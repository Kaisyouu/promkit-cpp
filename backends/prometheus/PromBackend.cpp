// Prometheus backend: single-process MVP using prometheus-cpp with config-based pre-registration

#include <promkit/promkit.hpp>
#include "core/Config.hpp"

#ifdef PROMKIT_BACKEND_PROM

#include <prometheus/exposer.h>
#include <prometheus/registry.h>
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include "mux/MuxCollector.hpp"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <unistd.h>
#include <vector>

namespace promkit {

namespace {

struct MetricSpec {
  std::string type; // counter|gauge|histogram
  std::map<std::string, std::string> const_labels; // always injected for this metric
  std::map<std::string, std::vector<std::string>> dyn; // allowed dynamic labels and values
  std::vector<double> buckets; // for histograms when provided
  bool has_buckets = false;
  std::string help;
};

struct Backend {
  std::unique_ptr<prometheus::Exposer> exposer;
  std::shared_ptr<prometheus::Registry> registry;

  // Families by full metric name
  std::mutex mu;
  std::map<std::string, prometheus::Family<prometheus::Counter>*> counters;
  std::map<std::string, prometheus::Family<prometheus::Gauge>*> gauges;
  std::map<std::string, prometheus::Family<prometheus::Histogram>*> histograms;

  // Pre-registered time series caches by key: name|k=v,k2=v2 (sorted by key)
  std::map<std::string, prometheus::Counter*> counter_series;
  std::map<std::string, prometheus::Gauge*> gauge_series;
  std::map<std::string, prometheus::Histogram*> hist_series;

  // Config & metric specs (from TOML)
  FileConfig fcfg;
  bool has_fcfg = false;
  std::map<std::string, MetricSpec> specs; // key: full metric name

  // Global config
  Config cfg;
  // Lifecycle state for safety around Shutdown/Reinit
  enum class State { Uninitialized, Running, ShuttingDown, Stopped };
  std::atomic<State> state{State::Uninitialized};

  // mux mode helpers
  bool mux_mode = false;         // cfg.mode == "mux"
  bool mux_aggregator = false;   // true if we own the public port
  std::string mux_dir;           // directory for worker descriptors
  std::string mux_worker_file;   // path to my descriptor file when worker
  std::shared_ptr<promkit::mux::MuxCollector> mux_collectable; // keep alive
};

Backend& G() {
  static Backend* inst = new Backend();
  return *inst;
}

static std::map<std::string, std::string> MergeLabels(const std::map<std::string, std::string>& a,
                                                      const std::map<std::string, std::string>& b) {
  auto out = a;
  for (auto& kv : b) out.emplace(kv.first, kv.second);
  return out;
}

static std::string FullName(const std::string& prefix, const std::string& name) {
  if (prefix.empty()) return name;
  return prefix + "_" + name;
}

static std::string LabelsKey(const std::map<std::string, std::string>& labels) {
  std::vector<std::pair<std::string,std::string>> v(labels.begin(), labels.end());
  std::sort(v.begin(), v.end(), [](auto& a, auto& b){ return a.first < b.first; });
  std::string key;
  for (size_t i = 0; i < v.size(); ++i) {
    if (i) key.push_back(',');
    key.append(v[i].first);
    key.push_back('=');
    key.append(v[i].second);
  }
  return key;
}

static std::vector<double> DefaultLatencyBuckets() {
  return {0.001, 0.005, 0.01, 0.05, 0.1, 0.25, 0.5, 1, 2};
}

// Clear all local caches/families/specs under lock. Does not touch registry/exposer.
static void ClearCachesLocked() {
  G().counters.clear();
  G().gauges.clear();
  G().histograms.clear();
  G().counter_series.clear();
  G().gauge_series.clear();
  G().hist_series.clear();
  G().specs.clear();
  G().has_fcfg = false;
}

// Helper: build mux worker directory path: /tmp/promkit-mux/<ns>
static std::string BuildMuxDir(const Config& cfg) {
  std::string ns = cfg.prefix; // may be empty
  if (ns.empty()) ns = "default";
  return std::string{"/tmp/promkit-mux/"} + ns;
}

static bool EnsureDir(const std::string& dir) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (fs::exists(dir, ec)) return true;
  return fs::create_directories(dir, ec);
}

static std::string MuxComponentName(const Config& cfg) {
  if (auto it = cfg.labels.find("component"); it != cfg.labels.end() && !it->second.empty()) return it->second;
  return std::string{"component-"} + std::to_string(::getpid());
}

static std::string WriteWorkerDescriptor(const Config& cfg, const std::string& dir, int port) {
  if (!EnsureDir(dir)) return {};
  const int pid = ::getpid();
  std::string file = dir + "/port." + std::to_string(pid);
  std::ofstream ofs(file, std::ios::trunc);
  if (!ofs) return {};
  ofs << "endpoint 127.0.0.1:" << port << "\n";
  ofs << "component " << MuxComponentName(cfg) << "\n";
  // pid 写入仅用于调试；后续聚合不再使用 pid 作为标签
  ofs << "pid " << pid << "\n";
  ofs << "path " << (cfg.path.empty() ? "/metrics" : cfg.path) << "\n";
  ofs.close();
  return file;
}

static prometheus::Family<prometheus::Counter>& GetOrMakeCounterFam(const std::string& fname, const std::string& help) {
  auto it = G().counters.find(fname);
  if (it != G().counters.end()) return *it->second;
  auto* fam = &prometheus::BuildCounter().Name(fname).Help(help).Register(*G().registry);
  G().counters.emplace(fname, fam);
  return *fam;
}

static prometheus::Family<prometheus::Gauge>& GetOrMakeGaugeFam(const std::string& fname, const std::string& help) {
  auto it = G().gauges.find(fname);
  if (it != G().gauges.end()) return *it->second;
  auto* fam = &prometheus::BuildGauge().Name(fname).Help(help).Register(*G().registry);
  G().gauges.emplace(fname, fam);
  return *fam;
}

static prometheus::Family<prometheus::Histogram>& GetOrMakeHistFam(const std::string& fname, const std::string& help) {
  auto it = G().histograms.find(fname);
  if (it != G().histograms.end()) return *it->second;
  auto* fam = &prometheus::BuildHistogram().Name(fname).Help(help).Register(*G().registry);
  G().histograms.emplace(fname, fam);
  return *fam;
}

static bool AllowedForMetric(const MetricSpec& spec, const std::map<std::string,std::string>& provided) {
  // Allowed keys are const_labels.keys U dyn.keys; values of dyn keys must be within the list.
  for (const auto& kv : provided) {
    const auto& k = kv.first;
    const auto& v = kv.second;
    if (spec.const_labels.count(k)) {
      if (spec.const_labels.at(k) != v) return false; // must match const label value
      continue;
    }
    auto dit = spec.dyn.find(k);
    if (dit == spec.dyn.end()) return false; // unknown key
    const auto& allowed = dit->second;
    if (std::find(allowed.begin(), allowed.end(), v) == allowed.end()) return false; // value not in enum
  }
  return true;
}

static void BuildDynCombos(const std::map<std::string, std::vector<std::string>>& dyn,
                           std::vector<std::map<std::string,std::string>>& out,
                           std::map<std::string,std::string> cur,
                           std::map<std::string, std::vector<std::string>>::const_iterator it,
                           std::map<std::string, std::vector<std::string>>::const_iterator end) {
  if (it == end) {
    out.emplace_back(std::move(cur));
    return;
  }
  const auto& key = it->first;
  const auto& vals = it->second;
  auto next = it; ++next;
  if (vals.empty()) {
    BuildDynCombos(dyn, out, cur, next, end);
    return;
  }
  for (const auto& v : vals) {
    auto cur2 = cur;
    cur2.emplace(key, v);
    BuildDynCombos(dyn, out, std::move(cur2), next, end);
  }
}

static void PreRegisterFromFileConfig() {
  // Build MetricSpec map and pre-register all time series combinations
  for (const auto& def : G().fcfg.metrics) {
    const auto fname = FullName(G().cfg.prefix, def.name);
    MetricSpec spec;
    spec.type = def.type;
    spec.const_labels = def.const_labels;
    spec.dyn = def.dynamic_labels;
    spec.help = def.help;
    spec.has_buckets = false;
    if (def.type == "histogram") {
      auto itb = G().fcfg.buckets.find(def.buckets_profile);
      if (itb != G().fcfg.buckets.end()) {
        spec.buckets = itb->second;
        spec.has_buckets = true;
      }
    }
    G().specs.emplace(fname, spec);

    // Pre-register
    auto base = MergeLabels(G().cfg.labels, def.const_labels);
    std::vector<std::map<std::string,std::string>> combos;
    if (!def.dynamic_labels.empty()) {
      BuildDynCombos(def.dynamic_labels, combos, {}, def.dynamic_labels.begin(), def.dynamic_labels.end());
    } else {
      combos.emplace_back(std::map<std::string,std::string>{});
    }

    std::lock_guard<std::mutex> lk(G().mu);
    if (def.type == "counter") {
      auto& fam = GetOrMakeCounterFam(fname, def.help);
      for (const auto& d : combos) {
        auto labels = MergeLabels(base, d);
        auto& ref = fam.Add(labels);
        G().counter_series.emplace(fname + "|" + LabelsKey(labels), &ref);
      }
    } else if (def.type == "gauge") {
      auto& fam = GetOrMakeGaugeFam(fname, def.help);
      for (const auto& d : combos) {
        auto labels = MergeLabels(base, d);
        auto& ref = fam.Add(labels);
        G().gauge_series.emplace(fname + "|" + LabelsKey(labels), &ref);
      }
    } else if (def.type == "histogram") {
      auto& fam = GetOrMakeHistFam(fname, def.help);
      const auto& buckets = spec.has_buckets ? spec.buckets : DefaultLatencyBuckets();
      for (const auto& d : combos) {
        auto labels = MergeLabels(base, d);
        auto& ref = fam.Add(labels, buckets);
        G().hist_series.emplace(fname + "|" + LabelsKey(labels), &ref);
      }
    }
  }
}

} // namespace

bool Init(const Config& cfg) noexcept {
  try {
    // If already running, perform a safe shutdown to allow re-init.
    auto prev = G().state.load(std::memory_order_acquire);
    if (prev == Backend::State::Running) {
      // Best-effort shutdown
      Shutdown();
    }

    G().cfg = cfg;
    G().mux_mode = (cfg.mode == "mux");
    if (!cfg.enabled) {
      G().state.store(Backend::State::Stopped, std::memory_order_release);
      return true; // disabled: still succeed
    }

    G().registry = std::make_shared<prometheus::Registry>();

    // mux mode: try aggregator first
    if (G().mux_mode) {
      try {
        // Try binding public port as aggregator
        std::string addr = cfg.host + ":" + std::to_string(cfg.port);
        G().exposer = std::make_unique<prometheus::Exposer>(addr);
        G().exposer->RegisterCollectable(G().registry, cfg.path.empty() ? std::string{"/metrics"} : cfg.path);
        // Register mux collector to same path
        G().mux_dir = BuildMuxDir(cfg);
        EnsureDir(G().mux_dir);
        G().mux_collectable = std::make_shared<promkit::mux::MuxCollector>();
        G().mux_collectable->SetDirectory(G().mux_dir);
        // 让聚合器自身也以 component 身份加入合并
        G().mux_collectable->SetSelf(G().registry, MuxComponentName(cfg));
        G().exposer->RegisterCollectable(G().mux_collectable, cfg.path.empty() ? std::string{"/metrics"} : cfg.path);
        G().mux_aggregator = true;
      } catch (...) {
        // Aggregator failed; become worker
        G().exposer.reset();
        std::string addr_local = std::string{"127.0.0.1:"} + std::to_string(0);
        G().exposer = std::make_unique<prometheus::Exposer>(addr_local);
        G().exposer->RegisterCollectable(G().registry, cfg.path.empty() ? std::string{"/metrics"} : cfg.path);
        auto ports = G().exposer->GetListeningPorts();
        int port = ports.empty() ? 0 : ports.front();
        if (port <= 0) throw std::runtime_error("failed to bind ephemeral port for worker");
        G().mux_dir = BuildMuxDir(cfg);
        G().mux_worker_file = WriteWorkerDescriptor(cfg, G().mux_dir, port);
        if (G().mux_worker_file.empty()) throw std::runtime_error("failed to write worker descriptor");
        G().mux_aggregator = false;
        G().state.store(Backend::State::Running, std::memory_order_release);
        return true;
      }

      // Aggregator success: register mux collector
      try {
        // Defer include to avoid header dependency error
        // We include header here
      } catch (...) {}
    }

    // single mode or mux aggregator fallback path: normal exposer
    if (!G().exposer) {
      std::string addr = cfg.host + ":" + std::to_string(cfg.port);
      G().exposer = std::make_unique<prometheus::Exposer>(addr);
      G().exposer->RegisterCollectable(G().registry, cfg.path.empty() ? std::string{"/metrics"} : cfg.path);
    }

    G().state.store(Backend::State::Running, std::memory_order_release);
    return true;
  } catch (...) {
    G().state.store(Backend::State::Stopped, std::memory_order_release);
    return false;
  }
}

bool InitFromToml(const std::string& toml_path) noexcept {
  // Parse TOML and then call Init, then pre-register metrics
  try {
    FileConfig fcfg;
    std::string err;
    if (!ParseConfigToml(toml_path, fcfg, err)) {
      return false;
    }
    Config cfg;
    cfg.enabled = fcfg.enabled;
    cfg.mode    = fcfg.mode;
    cfg.host    = fcfg.host;
    cfg.port    = fcfg.port;
    cfg.path    = fcfg.path;
    cfg.prefix  = fcfg.ns;
    cfg.labels  = fcfg.labels;
    if (!Init(cfg)) return false;

    // Save config and pre-register time series based on definitions
    G().fcfg = std::move(fcfg);
    G().has_fcfg = true;
    if (G().state.load(std::memory_order_acquire) == Backend::State::Running) {
      PreRegisterFromFileConfig();
    }
    return true;
  } catch (...) {
    return false;
  }
}

void Shutdown() noexcept {
  try {
    // Transition to shutting down to gate all API calls.
    G().state.store(Backend::State::ShuttingDown, std::memory_order_release);
    G().cfg.enabled = false; // extra guard for older checks

    // Clear caches under lock so concurrent creators won't deref stale pointers.
    {
      std::lock_guard<std::mutex> lk(G().mu);
      ClearCachesLocked();
    }

    // Remove worker descriptor if any
    try {
      if (!G().mux_worker_file.empty()) {
        std::error_code ec; std::filesystem::remove(G().mux_worker_file, ec);
        G().mux_worker_file.clear();
      }
    } catch (...) {}

    // Tear down prometheus-cpp objects after caches are cleared.
    G().exposer.reset();
    G().registry.reset();

    G().state.store(Backend::State::Stopped, std::memory_order_release);
  } catch (...) {
    G().state.store(Backend::State::Stopped, std::memory_order_release);
  }
}

bool IsRunning() noexcept {
  try {
    return G().cfg.enabled && G().state.load(std::memory_order_acquire) == Backend::State::Running;
  } catch (...) {
    return false;
  }
}

CounterId CreateCounter(const std::string& name, const std::string& help,
                        const std::map<std::string, std::string>& const_labels) noexcept {
  if (!G().cfg.enabled || G().state.load(std::memory_order_acquire) != Backend::State::Running) return 0;
  try {
    const auto fname = FullName(G().cfg.prefix, name);
    std::map<std::string, std::string> final_labels = MergeLabels(G().cfg.labels, const_labels);

    std::lock_guard<std::mutex> lk(G().mu);
    if (G().state.load(std::memory_order_relaxed) != Backend::State::Running) return 0;
    // If spec exists, enforce allowed labels and inject spec.const_labels
    auto sit = G().specs.find(fname);
    if (sit != G().specs.end()) {
      for (const auto& kv : sit->second.const_labels) final_labels.emplace(kv.first, kv.second);
      if (!AllowedForMetric(sit->second, const_labels)) return 0; // reject
      const auto key = fname + "|" + LabelsKey(final_labels);
      if (auto ts = G().counter_series.find(key); ts != G().counter_series.end()) {
        return reinterpret_cast<CounterId>(ts->second);
      }
      // If not found, and metric was defined, do not create new dynamic series; reject
      return 0;
    }
    // No spec: create ad-hoc
    auto& fam = GetOrMakeCounterFam(fname, help);
    auto& ref = fam.Add(final_labels);
    return reinterpret_cast<CounterId>(&ref);
  } catch (...) {
    return 0;
  }
}

void CounterAdd(CounterId id, double value) noexcept {
  if (!G().cfg.enabled || id == 0 || G().state.load(std::memory_order_acquire) != Backend::State::Running) return;
  auto* c = reinterpret_cast<prometheus::Counter*>(id);
  if (value > 0) c->Increment(value);
}

GaugeId CreateGauge(const std::string& name, const std::string& help,
                    const std::map<std::string, std::string>& const_labels) noexcept {
  if (!G().cfg.enabled || G().state.load(std::memory_order_acquire) != Backend::State::Running) return 0;
  try {
    const auto fname = FullName(G().cfg.prefix, name);
    std::map<std::string, std::string> final_labels = MergeLabels(G().cfg.labels, const_labels);
    std::lock_guard<std::mutex> lk(G().mu);
    if (G().state.load(std::memory_order_relaxed) != Backend::State::Running) return 0;
    auto sit = G().specs.find(fname);
    if (sit != G().specs.end()) {
      for (const auto& kv : sit->second.const_labels) final_labels.emplace(kv.first, kv.second);
      if (!AllowedForMetric(sit->second, const_labels)) return 0;
      const auto key = fname + "|" + LabelsKey(final_labels);
      if (auto ts = G().gauge_series.find(key); ts != G().gauge_series.end()) {
        return reinterpret_cast<GaugeId>(ts->second);
      }
      return 0;
    }
    auto& fam = GetOrMakeGaugeFam(fname, help);
    auto& ref = fam.Add(final_labels);
    return reinterpret_cast<GaugeId>(&ref);
  } catch (...) {
    return 0;
  }
}

void GaugeSet(GaugeId id, double value) noexcept {
  if (!G().cfg.enabled || id == 0 || G().state.load(std::memory_order_acquire) != Backend::State::Running) return;
  auto* g = reinterpret_cast<prometheus::Gauge*>(id);
  g->Set(value);
}

void GaugeAdd(GaugeId id, double delta) noexcept {
  if (!G().cfg.enabled || id == 0 || G().state.load(std::memory_order_acquire) != Backend::State::Running) return;
  auto* g = reinterpret_cast<prometheus::Gauge*>(id);
  if (delta >= 0) g->Increment(delta); else g->Decrement(-delta);
}

HistogramId CreateHistogram(const std::string& name, const std::string& help,
                            const std::vector<double>& buckets,
                            const std::map<std::string, std::string>& const_labels) noexcept {
  if (!G().cfg.enabled || G().state.load(std::memory_order_acquire) != Backend::State::Running) return 0;
  try {
    const auto fname = FullName(G().cfg.prefix, name);
    std::map<std::string, std::string> final_labels = MergeLabels(G().cfg.labels, const_labels);
    std::lock_guard<std::mutex> lk(G().mu);
    if (G().state.load(std::memory_order_relaxed) != Backend::State::Running) return 0;
    auto sit = G().specs.find(fname);
    if (sit != G().specs.end()) {
      for (const auto& kv : sit->second.const_labels) final_labels.emplace(kv.first, kv.second);
      if (!AllowedForMetric(sit->second, const_labels)) return 0;
      const auto key = fname + "|" + LabelsKey(final_labels);
      if (auto ts = G().hist_series.find(key); ts != G().hist_series.end()) {
        return reinterpret_cast<HistogramId>(ts->second);
      }
      return 0;
    }
    auto& fam = GetOrMakeHistFam(fname, help);
    const auto& used_buckets = buckets.empty() ? DefaultLatencyBuckets() : buckets;
    auto& ref = fam.Add(final_labels, used_buckets);
    return reinterpret_cast<HistogramId>(&ref);
  } catch (...) {
    return 0;
  }
}

void HistogramObserve(HistogramId id, double value) noexcept {
  if (!G().cfg.enabled || id == 0 || G().state.load(std::memory_order_acquire) != Backend::State::Running) return;
  auto* h = reinterpret_cast<prometheus::Histogram*>(id);
  h->Observe(value);
}

} // namespace promkit

#else // PROMKIT_BACKEND_PROM

namespace promkit {
bool Init(const Config&) noexcept { return true; }
bool InitFromToml(const std::string&) noexcept { return true; }
void Shutdown() noexcept {}
bool IsRunning() noexcept { return false; }
CounterId CreateCounter(const std::string&, const std::string&, const std::map<std::string, std::string>&) noexcept { return 0; }
void CounterAdd(CounterId, double) noexcept {}
GaugeId CreateGauge(const std::string&, const std::string&, const std::map<std::string, std::string>&) noexcept { return 0; }
void GaugeSet(GaugeId, double) noexcept {}
void GaugeAdd(GaugeId, double) noexcept {}
HistogramId CreateHistogram(const std::string&, const std::string&, const std::vector<double>&, const std::map<std::string, std::string>&) noexcept { return 0; }
void HistogramObserve(HistogramId, double) noexcept {}
} // namespace promkit

#endif
