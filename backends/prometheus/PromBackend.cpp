// Prometheus backend: minimal single-process MVP using prometheus-cpp

#include <promkit/promkit.hpp>
#include "core/Config.hpp"

#ifdef PROMKIT_BACKEND_PROM

#include <prometheus/exposer.h>
#include <prometheus/registry.h>
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace promkit {

namespace {

struct Backend {
  std::unique_ptr<prometheus::Exposer> exposer;
  std::shared_ptr<prometheus::Registry> registry;

  // Families by full metric name
  std::mutex mu;
  std::map<std::string, prometheus::Family<prometheus::Counter>*> counters;
  std::map<std::string, prometheus::Family<prometheus::Gauge>*> gauges;
  std::map<std::string, prometheus::Family<prometheus::Histogram>*> histograms;

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

} // namespace

bool Init(const Config& cfg) noexcept {
  try {
    G().cfg = cfg;
    if (!cfg.enabled) return true; // disabled: still succeed

    G().registry = std::make_shared<prometheus::Registry>();

    // Build exposer address
    std::string addr = cfg.host + ":" + std::to_string(cfg.port);
    G().exposer = std::make_unique<prometheus::Exposer>(addr);
    // Register /metrics endpoint (default collector)
    G().exposer->RegisterCollectable(G().registry);

    return true;
  } catch (...) {
    return false;
  }
}

bool InitFromToml(const std::string& toml_path) noexcept {
  // Minimal bridge: parse TOML and then call Init
  try {
    promkit::FileConfig fcfg;
    std::string err;
    if (!promkit::ParseConfigToml(toml_path, fcfg, err)) {
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
    return Init(cfg);
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
    std::lock_guard<std::mutex> lk(G().mu);
    auto it = G().counters.find(fname);
    prometheus::Family<prometheus::Counter>* fam = nullptr;
    if (it == G().counters.end()) {
      fam = &prometheus::BuildCounter().Name(fname).Help(help).Register(*G().registry);
      G().counters.emplace(fname, fam);
    } else {
      fam = it->second;
    }
    auto labels = MergeLabels(G().cfg.labels, const_labels);
    auto& counter = fam->Add(labels);
    return reinterpret_cast<CounterId>(&counter);
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
    std::lock_guard<std::mutex> lk(G().mu);
    auto it = G().gauges.find(fname);
    prometheus::Family<prometheus::Gauge>* fam = nullptr;
    if (it == G().gauges.end()) {
      fam = &prometheus::BuildGauge().Name(fname).Help(help).Register(*G().registry);
      G().gauges.emplace(fname, fam);
    } else {
      fam = it->second;
    }
    auto labels = MergeLabels(G().cfg.labels, const_labels);
    auto& gauge = fam->Add(labels);
    return reinterpret_cast<GaugeId>(&gauge);
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
    std::lock_guard<std::mutex> lk(G().mu);
    auto it = G().histograms.find(fname);
    prometheus::Family<prometheus::Histogram>* fam = nullptr;
    if (it == G().histograms.end()) {
      fam = &prometheus::BuildHistogram().Name(fname).Help(help).Register(*G().registry);
      G().histograms.emplace(fname, fam);
    } else {
      fam = it->second;
    }
    auto labels = MergeLabels(G().cfg.labels, const_labels);
    auto& hist = fam->Add(labels, buckets);
    return reinterpret_cast<HistogramId>(&hist);
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
