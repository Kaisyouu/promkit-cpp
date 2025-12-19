// TOML config loader using toml++ (header-only)
#include "Config.hpp"

#include <string>
#include <vector>
#include <map>

#include <toml++/toml.h>

namespace promkit {

static inline std::string as_string_or(const toml::node_view<toml::node>& nv, const std::string& def) {
  if (!nv) return def;
  if (auto v = nv.value<std::string>()) return *v;
  return def;
}

static inline int as_int_or(const toml::node_view<toml::node>& nv, int def) {
  if (!nv) return def;
  if (auto v = nv.value<int64_t>()) return static_cast<int>(*v);
  if (auto v2 = nv.value<int>()) return *v2;
  return def;
}

static inline bool as_bool_or(const toml::node_view<toml::node>& nv, bool def) {
  if (!nv) return def;
  if (auto v = nv.value<bool>()) return *v;
  return def;
}

bool ParseConfigToml(const std::string& path, FileConfig& out, std::string& err) {
  try {
    auto tbl = toml::parse_file(path);

    // exporter
    if (auto exporter = tbl["exporter"]; exporter.is_table()) {
      out.enabled = as_bool_or(exporter["enabled"], true);
      out.mode    = as_string_or(exporter["mode"], "single");
      out.host    = as_string_or(exporter["host"], "0.0.0.0");
      out.port    = as_int_or(exporter["port"], 9464);
      out.path    = as_string_or(exporter["path"], "/metrics");
      out.ns      = as_string_or(exporter["namespace"], "");
    }

    // labels
    if (auto labels = tbl["labels"]; labels.is_table()) {
      for (auto&& [k,v] : *labels.as_table()) {
        if (auto s = v.value<std::string>()) out.labels.emplace(k, *s);
      }
    }

    // buckets
    if (auto buckets = tbl["buckets"]; buckets.is_table()) {
      for (auto&& [k,v] : *buckets.as_table()) {
        if (auto arr = v.as_array()) {
          std::vector<double> vec;
          vec.reserve(arr->size());
          for (auto&& el : *arr) {
            if (auto d = el.value<double>()) vec.push_back(*d);
          }
          out.buckets.emplace(k, std::move(vec));
        }
      }
    }

    // metrics
    if (auto metrics = tbl["metrics"]; metrics.is_array()) {
      for (auto&& m : *metrics.as_array()) {
        if (!m.is_table()) continue;
        MetricDef def;
        auto mt = *m.as_table();
        def.name = as_string_or(mt["name"], "");
        def.type = as_string_or(mt["type"], "");
        def.help = as_string_or(mt["help"], "");
        def.unit = as_string_or(mt["unit"], "");
        def.buckets_profile = as_string_or(mt["buckets_profile"], "");
        def.publish = as_string_or(mt["publish"], "");
        def.gauge_agg = as_string_or(mt["gauge_agg"], "");

        if (auto cl = mt["const_labels"]; cl.is_table()) {
          for (auto&& [k,v] : *cl.as_table()) {
            if (auto s = v.value<std::string>()) def.const_labels.emplace(k, *s);
          }
        }
        if (auto dl = mt["dynamic_labels"]; dl.is_table()) {
          for (auto&& [k,v] : *dl.as_table()) {
            if (auto arr = v.as_array()) {
              std::vector<std::string> vals;
              vals.reserve(arr->size());
              for (auto&& el : *arr) if (auto s = el.value<std::string>()) vals.push_back(*s);
              def.dynamic_labels.emplace(k, std::move(vals));
            }
          }
        }
        if (!def.name.empty() && !def.type.empty()) out.metrics.emplace_back(std::move(def));
      }
    }

    return true;
  } catch (const std::exception& e) {
    err = e.what();
  } catch (...) {
    err = "unknown error";
  }
  return false;
}

} // namespace promkit
