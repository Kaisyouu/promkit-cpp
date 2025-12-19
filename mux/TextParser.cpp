#include "TextParser.hpp"

#include <prometheus/metric_family.h>
#include <prometheus/metric_type.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <limits>
#include <unordered_map>

namespace promkit::mux {

namespace {

static inline std::string trim(std::string_view sv) {
  auto l = sv.find_first_not_of(" \t\r\n");
  if (l == std::string_view::npos) return {};
  auto r = sv.find_last_not_of(" \t\r\n");
  return std::string(sv.substr(l, r - l + 1));
}

static bool parseLabels(std::string_view sv, std::vector<prometheus::ClientMetric::Label>& out) {
  // format: {k="v",k2="v2"}
  out.clear();
  if (sv.size() < 2 || sv.front() != '{' || sv.back() != '}') return false;
  sv.remove_prefix(1);
  sv.remove_suffix(1);
  while (!sv.empty()) {
    auto comma = sv.find(',');
    auto token = sv.substr(0, comma);
    if (comma != std::string_view::npos) sv.remove_prefix(comma + 1); else sv = {};
    auto eq = token.find('=');
    if (eq == std::string_view::npos) break;
    auto k = trim(token.substr(0, eq));
    auto v = token.substr(eq + 1);
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size()-2);
    out.push_back({std::string(k), std::string(v)});
  }
  return true;
}

static bool parseNumber(std::string_view sv, double& out) {
  // Allow NaN/Inf; for simplicity, use stod fallback.
  try {
    std::string s(sv);
    out = std::stod(s);
    return true;
  } catch (...) {
    return false;
  }
}

} // namespace

std::vector<prometheus::MetricFamily> ParseTextExposition(const std::string& text) {
  std::vector<prometheus::MetricFamily> fams;
  std::istringstream iss(text);
  std::string line;
  // Map by base name without suffix (_bucket/_sum/_count for histogram)
  std::unordered_map<std::string, prometheus::MetricType> ty_map;
  std::unordered_map<std::string, std::string> help_map;
  auto getFam = [&](const std::string& name, prometheus::MetricType ty, const std::string& help) -> prometheus::MetricFamily& {
    for (auto& f : fams) if (f.name == name) return f;
    fams.push_back({});
    auto& f = fams.back();
    f.name = name; f.help = help; f.type = ty;
    return f;
  };

  while (std::getline(iss, line)) {
    if (line.empty() || line[0] == '#') continue;
    // name[labels] value [timestamp]
    std::string name;
    std::vector<prometheus::ClientMetric::Label> labels;
    double value = 0;

    std::string_view sv(line);
    // name
    size_t i = 0;
    while (i < sv.size() && (std::isalnum((unsigned char)sv[i]) || sv[i]=='_' || sv[i]==':')) ++i;
    name = std::string(sv.substr(0, i));
    sv.remove_prefix(i);
    // labels
    if (!sv.empty() && sv.front() == '{') {
      auto end = sv.find('}');
      if (end == std::string_view::npos) continue;
      auto lab = sv.substr(0, end + 1);
      std::vector<prometheus::ClientMetric::Label> tmp;
      if (!parseLabels(lab, tmp)) continue;
      labels = std::move(tmp);
      sv.remove_prefix(end + 1);
    }
    // space then value
    auto p = sv.find_first_not_of(" \t");
    if (p == std::string_view::npos) continue;
    sv.remove_prefix(p);
    // value up to space
    auto q = sv.find_first_of(" \t");
    auto vs = sv.substr(0, q);
    if (!parseNumber(vs, value)) continue;

    // histogram special cases
    if (name.size() > 7 && name.ends_with("_bucket")) {
      auto base = name.substr(0, name.size()-7);
      // find le label; move it to bucket
      prometheus::ClientMetric m;
      m.label = labels;
      double le = 0;
      for (auto it = m.label.begin(); it != m.label.end(); ++it) {
        if (it->name == "le") {
          try { le = std::stod(it->value); } catch (...) { le = std::numeric_limits<double>::infinity(); }
          m.label.erase(it);
          break;
        }
      }
      auto& f = getFam(base, prometheus::MetricType::Histogram, "");
      // find or create metric with same labels
      auto sameLabels = [&](const prometheus::ClientMetric& x){ return x.label == m.label; };
      auto it = std::find_if(f.metric.begin(), f.metric.end(), sameLabels);
      if (it == f.metric.end()) { f.metric.push_back({}); it = f.metric.end()-1; it->label = m.label; }
      prometheus::ClientMetric::Bucket b; b.upper_bound = le; b.cumulative_count = static_cast<std::uint64_t>(value);
      it->histogram.bucket.push_back(b);
      continue;
    }
    if (name.size() > 4 && name.ends_with("_sum")) {
      auto base = name.substr(0, name.size()-4);
      auto& f = getFam(base, prometheus::MetricType::Histogram, "");
      prometheus::ClientMetric m; m.label = labels; m.histogram.sample_sum = value;
      // merge into existing metric
      auto it = std::find_if(f.metric.begin(), f.metric.end(), [&](const auto& x){ return x.label == m.label; });
      if (it == f.metric.end()) f.metric.push_back(m); else it->histogram.sample_sum = m.histogram.sample_sum;
      continue;
    }
    if (name.size() > 6 && name.ends_with("_count")) {
      auto base = name.substr(0, name.size()-6);
      auto& f = getFam(base, prometheus::MetricType::Histogram, "");
      prometheus::ClientMetric m; m.label = labels; m.histogram.sample_count = static_cast<std::uint64_t>(value);
      auto it = std::find_if(f.metric.begin(), f.metric.end(), [&](const auto& x){ return x.label == m.label; });
      if (it == f.metric.end()) f.metric.push_back(m); else it->histogram.sample_count = m.histogram.sample_count;
      continue;
    }

    // consult TYPE map if known
    auto tyit = ty_map.find(name);
    auto ty = (tyit == ty_map.end()) ? prometheus::MetricType::Untyped : tyit->second;
    auto hlpit = help_map.find(name);
    auto& f = getFam(name, ty, (hlpit==help_map.end()?std::string():hlpit->second));
    prometheus::ClientMetric m; m.label = labels; m.untyped.value = value;
    f.metric.push_back(std::move(m));
  }

  // Sort histogram buckets by le
  for (auto& f : fams) if (f.type == prometheus::MetricType::Histogram) {
    for (auto& m : f.metric) {
      std::sort(m.histogram.bucket.begin(), m.histogram.bucket.end(), [](auto& a, auto& b){ return a.upper_bound < b.upper_bound; });
    }
  }
  return fams;
}

} // namespace promkit::mux
