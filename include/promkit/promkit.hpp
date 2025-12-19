#pragma once
// promkit-cpp public API (MVP)
// - Single-process mode only for now (mux in later versions)
// - Programmatic config (TOML file support to be added later)
// - Opaque metric ids to avoid exposing prometheus-cpp types in public headers

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <chrono>

namespace promkit {

struct Config {
  bool enabled = true;              // when false, all APIs are no-op
  std::string mode = "single";      // "single" | "mux" (mux not implemented yet)
  std::string host = "0.0.0.0";     // bind host for HTTP exposer
  int         port = 9464;          // bind port for HTTP exposer
  std::string path = "/metrics";   // metrics path
  std::string prefix;               // optional name prefix: <prefix>_<metric>
  std::map<std::string, std::string> labels; // global labels injected to every series
};

using CounterId = std::uint64_t;
using GaugeId = std::uint64_t;
using HistogramId = std::uint64_t;

// Lifecycle
bool Init(const Config& cfg) noexcept;
// Init from TOML path (uses core::ParseConfigToml internally); returns false on parse or init failure.
bool InitFromToml(const std::string& toml_path) noexcept;
void Shutdown() noexcept;

// Returns whether the backend is currently running (thread-safe).
// Useful for guarding calls in apps that may ShutDown while worker threads are still draining.
bool IsRunning() noexcept;

// Counters
CounterId CreateCounter(const std::string& name,
                        const std::string& help,
                        const std::map<std::string, std::string>& const_labels = {}) noexcept;
void CounterAdd(CounterId id, double value = 1.0) noexcept;  // value >= 0

// Gauges
GaugeId CreateGauge(const std::string& name,
                    const std::string& help,
                    const std::map<std::string, std::string>& const_labels = {}) noexcept;
void GaugeSet(GaugeId id, double value) noexcept;
void GaugeAdd(GaugeId id, double delta) noexcept; // inc/dec via +/-

// Histograms
HistogramId CreateHistogram(const std::string& name,
                            const std::string& help,
                            const std::vector<double>& buckets,
                            const std::map<std::string, std::string>& const_labels = {}) noexcept;
void HistogramObserve(HistogramId id, double value) noexcept;

// RAII timer for latency (observes on destruction)
class ScopeTimer {
 public:
  explicit ScopeTimer(HistogramId hid) noexcept : hid_(hid), start_(Clock::now()) {}
  ~ScopeTimer() noexcept {
    if (hid_ != 0) {
      auto elapsed = std::chrono::duration<double>(Clock::now() - start_).count();
      HistogramObserve(hid_, elapsed);
    }
  }
  // Disallow copy; allow move
  ScopeTimer(const ScopeTimer&) = delete;
  ScopeTimer& operator=(const ScopeTimer&) = delete;
  ScopeTimer(ScopeTimer&& other) noexcept : hid_(other.hid_), start_(other.start_) { other.hid_ = 0; }
  ScopeTimer& operator=(ScopeTimer&& other) noexcept {
    if (this != &other) { hid_ = other.hid_; start_ = other.start_; other.hid_ = 0; }
    return *this;
  }
 private:
  using Clock = std::chrono::steady_clock;
  HistogramId hid_ = 0;
  Clock::time_point start_{};
};

} // namespace promkit
