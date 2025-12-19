// Prometheus backend: single-process MVP using prometheus-cpp with config-based pre-registration

#include <promkit/promkit.hpp>
#include "core/Config.hpp"

#ifdef PROMKIT_BACKEND_PROM

#include <prometheus/exposer.h>
#include <prometheus/registry.h>
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
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
    G().cfg = cfg;
    if (!cfg.enabled) return true; // disabled: still succeed

    G().registry = std::make_shared<prometheus::Registry>();

    // Build exposer address
    std::string addr = cfg.host + ":" + std::to_string(cfg.port);
    G().exposer = std::make_unique<prometheus::Exposer>(addr);
    // Register /metrics endpoint (respect configured path)
    G().exposer->RegisterCollectable(G().registry, cfg.path.empty() ? std::string{"/metrics"} : cfg.path);

    return true;
  } catch (...) {
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
    PreRegisterFromFileConfig();
    return true;
  } catch (...) {
    return false;
  }
}

void Shutdown() noexcept {
  try {
    G().exposer.reset();
    G().registry.reset();
  } catch (...) {
  }
}

CounterId CreateCounter(const std::string& name, const std::string& help,
                        const std::map<std::string, std::string>& const_labels) noexcept {
  if (!G().cfg.enabled) return 0;
  try {
    const auto fname = FullName(G().cfg.prefix, name);
    std::map<std::string, std::string> final_labels = MergeLabels(G().cfg.labels, const_labels);

    std::lock_guard<std::mutex> lk(G().mu);
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
  if (!G().cfg.enabled || id == 0) return;
  auto* c = reinterpret_cast<prometheus::Counter*>(id);
  if (value > 0) c->Increment(value);
}

GaugeId CreateGauge(const std::string& name, const std::string& help,
                    const std::map<std::string, std::string>& const_labels) noexcept {
  if (!G().cfg.enabled) return 0;
  try {
    const auto fname = FullName(G().cfg.prefix, name);
    std::map<std::string, std::string> final_labels = MergeLabels(G().cfg.labels, const_labels);
    std::lock_guard<std::mutex> lk(G().mu);
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
  if (!G().cfg.enabled || id == 0) return;
  auto* g = reinterpret_cast<prometheus::Gauge*>(id);
  g->Set(value);
}

void GaugeAdd(GaugeId id, double delta) noexcept {
  if (!G().cfg.enabled || id == 0) return;
  auto* g = reinterpret_cast<prometheus::Gauge*>(id);
  if (delta >= 0) g->Increment(delta); else g->Decrement(-delta);
}

HistogramId CreateHistogram(const std::string& name, const std::string& help,
                            const std::vector<double>& buckets,
                            const std::map<std::string, std::string>& const_labels) noexcept {
  if (!G().cfg.enabled) return 0;
  try {
    const auto fname = FullName(G().cfg.prefix, name);
    std::map<std::string, std::string> final_labels = MergeLabels(G().cfg.labels, const_labels);
    std::lock_guard<std::mutex> lk(G().mu);
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
  if (!G().cfg.enabled || id == 0) return;
  auto* h = reinterpret_cast<prometheus::Histogram*>(id);
  h->Observe(value);
}

} // namespace promkit

#else // PROMKIT_BACKEND_PROM

namespace promkit {
bool Init(const Config&) noexcept { return true; }
bool InitFromToml(const std::string&) noexcept { return true; }
void Shutdown() noexcept {}
CounterId CreateCounter(const std::string&, const std::string&, const std::map<std::string, std::string>&) noexcept { return 0; }
void CounterAdd(CounterId, double) noexcept {}
GaugeId CreateGauge(const std::string&, const std::string&, const std::map<std::string, std::string>&) noexcept { return 0; }
void GaugeSet(GaugeId, double) noexcept {}
void GaugeAdd(GaugeId, double) noexcept {}
HistogramId CreateHistogram(const std::string&, const std::string&, const std::vector<double>&, const std::map<std::string, std::string>&) noexcept { return 0; }
void HistogramObserve(HistogramId, double) noexcept {}
} // namespace promkit

#endif
