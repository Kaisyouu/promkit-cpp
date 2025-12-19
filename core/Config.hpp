// Config parsing and structures (header-only for now)
#pragma once
#include <string>
#include <vector>
#include <map>

namespace promkit {

struct MetricDef {
  std::string name;
  std::string type;       // counter|gauge|histogram
  std::string help;
  std::string unit;       // annotation only
  std::map<std::string, std::string> const_labels;
  std::map<std::string, std::vector<std::string>> dynamic_labels; // reserved for future
  std::string buckets_profile; // for histograms
  std::string publish;    // sum_only|per_proc|both (default inherited)
  std::string gauge_agg;  // sum|last|max (gauge only)
};

struct FileConfig {
  // exporter
  bool        enabled = true;
  std::string mode    = "single"; // single|mux
  std::string host    = "0.0.0.0";
  int         port    = 9464;
  std::string path    = "/metrics";
  std::string ns;                 // namespace/prefix

  // labels
  std::map<std::string, std::string> labels; // service/component/env/version/instance/proc

  // buckets profiles
  std::map<std::string, std::vector<double>> buckets;

  // metrics
  std::vector<MetricDef> metrics;
};

// Try to parse TOML file into FileConfig. Returns true on success, false on failure.
bool ParseConfigToml(const std::string& path, FileConfig& out, std::string& err);

} // namespace promkit
