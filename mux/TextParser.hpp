// Minimal text exposition parser: parses Prometheus text format into MetricFamily
#pragma once
#include <prometheus/metric_family.h>
#include <string>
#include <vector>

namespace promkit::mux {

// Parse Prometheus text exposition to families. Best-effort; ignores HELP/TYPE.
// Returns empty on parse error. Only supports counter, gauge, histogram lines.
std::vector<prometheus::MetricFamily> ParseTextExposition(const std::string& text);

} // namespace promkit::mux

