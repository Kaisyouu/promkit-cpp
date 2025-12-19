// Noop backend implementation: all APIs are stubs
#include <promkit/promkit.hpp>

namespace promkit {

bool Init(const Config&) noexcept { return true; }
bool InitFromToml(const std::string&) noexcept { return true; }
void Shutdown() noexcept {}
bool IsRunning() noexcept { return false; }

CounterId CreateCounter(const std::string&, const std::string&, const std::map<std::string, std::string>&) noexcept {
  return 0; // invalid id
}

void CounterAdd(CounterId, double) noexcept {}

GaugeId CreateGauge(const std::string&, const std::string&, const std::map<std::string, std::string>&) noexcept {
  return 0;
}

void GaugeSet(GaugeId, double) noexcept {}
void GaugeAdd(GaugeId, double) noexcept {}

HistogramId CreateHistogram(const std::string&, const std::string&, const std::vector<double>&,
                            const std::map<std::string, std::string>&) noexcept {
  return 0;
}

void HistogramObserve(HistogramId, double) noexcept {}

} // namespace promkit
