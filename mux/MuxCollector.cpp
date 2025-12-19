#include "MuxCollector.hpp"
#include "TextParser.hpp"

#include <prometheus/metric_family.h>
#include <prometheus/text_serializer.h>
#include <prometheus/registry.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <unordered_map>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#else
#  include <sys/socket.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <signal.h>
#endif

namespace {
#ifdef _WIN32
struct WsaInit { WsaInit(){ WSADATA d; WSAStartup(MAKEWORD(2,2), &d);} ~WsaInit(){ WSACleanup(); } };
static WsaInit g_wsa_init; // ensure WSA is initialized once
using socket_t = SOCKET;
constexpr socket_t invalid_socket = INVALID_SOCKET;
inline void closesock(socket_t s){ if(s!=INVALID_SOCKET) ::closesocket(s); }
static bool PidAlive(int pid) {
  HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
  if (!h) return false;
  DWORD code = 0; BOOL ok = ::GetExitCodeProcess(h, &code);
  ::CloseHandle(h);
  return ok && code == STILL_ACTIVE;
}
#else
using socket_t = int;
constexpr socket_t invalid_socket = -1;
inline void closesock(socket_t s){ if(s>=0) ::close(s); }
static bool PidAlive(int pid) { return ::kill(pid, 0) == 0; }
#endif
} // anonymous

namespace fs = std::filesystem;

namespace promkit::mux {

void MuxCollector::SetDirectory(std::string dir) { dir_ = std::move(dir); }
void MuxCollector::SetWorkers(std::vector<WorkerEndpoint> workers) { workers_ = std::move(workers); }
void MuxCollector::SetSelf(std::shared_ptr<prometheus::Registry> self, std::string component) {
  self_ = std::move(self);
  self_component_ = std::move(component);
}

static std::vector<WorkerEndpoint> ScanDir(const std::string& dir) {
  std::vector<WorkerEndpoint> out;
  if (dir.empty()) return out;
  std::error_code ec;
  if (!fs::exists(dir, ec)) return out;
  for (auto& de : fs::directory_iterator(dir, ec)) {
    if (!de.is_regular_file()) continue;
    // File format: endpoint host:port\ncomponent <name>\npid <pid>\npath /metrics
    std::ifstream ifs(de.path());
    if (!ifs) continue;
    WorkerEndpoint we; we.host = "127.0.0.1"; we.port = 0; we.path = "/metrics";
    std::string line;
    while (std::getline(ifs, line)) {
      if (line.starts_with("endpoint ")) {
        auto e = line.substr(9);
        auto colon = e.find(':');
        if (colon != std::string::npos) { we.host = e.substr(0, colon); we.port = std::stoi(e.substr(colon+1)); }
      } else if (line.starts_with("component ")) {
        we.component = line.substr(10);
      } else if (line.starts_with("pid ")) {
        we.pid = std::stoi(line.substr(4));
      } else if (line.starts_with("path ")) {
        we.path = line.substr(5);
      }
    }
    // Prune stale descriptor: pid not alive
    if (we.pid > 0) {
      if (!PidAlive(we.pid)) {
        std::error_code ec2; fs::remove(de.path(), ec2);
        continue;
      }
    }
    if (we.port > 0 && !we.component.empty()) out.push_back(std::move(we));
  }
  return out;
}

static std::string HttpGetLocal(const WorkerEndpoint& we) {
  // Very naive: use std::ifstream on /proc/self/fd? No.
  // For initial version, we rely on curl-like availability is not guaranteed.
  // So we use a minimal blocking socket HTTP/1.0 GET.
  // Note: this is intentionally simple and localhost-only.
  std::ostringstream out;
  try {
    socket_t sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock == invalid_socket) return {};
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(we.port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
    if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) { closesock(sock); return {}; }
    std::string req = "GET " + we.path + " HTTP/1.0\r\nHost: " + we.host + "\r\nConnection: close\r\n\r\n";
    ::send(sock, req.data(), (int)req.size(), 0);
    char buf[4096];
    std::string resp;
    int n;
    while ((n = ::recv(sock, buf, (int)sizeof(buf), 0)) > 0) resp.append(buf, buf + n);
    closesock(sock);
    // strip headers
    auto pos = resp.find("\r\n\r\n");
    if (pos != std::string::npos) resp = resp.substr(pos + 4);
    return resp;
  } catch (...) {
    return {};
  }
}

std::vector<prometheus::MetricFamily> MuxCollector::Collect() const {
  std::vector<WorkerEndpoint> ws = workers_;
  if (ws.empty() && !dir_.empty()) ws = ScanDir(dir_);
  // Merge by family name/type: append series across workers
  std::vector<prometheus::MetricFamily> merged;
  auto findFam = [&](const std::string& name, prometheus::MetricType ty) -> prometheus::MetricFamily* {
    for (auto& f : merged) if (f.name == name && f.type == ty) return &f;
    merged.push_back({});
    merged.back().name = name; merged.back().type = ty; return &merged.back();
  };
  // 鍏堟敹闆?aggregator 鑷韩 registry 鐨勬寚鏍囷紙labels.component 宸茬敱搴撴敞鍏ワ紝涓嶅啀閲嶅娉ㄥ叆锛?
  if (auto self = self_.lock()) {
    auto self_fams = self->Collect();
    for (auto& f : self_fams) {
      auto* dst = findFam(f.name, f.type);
      if (dst->help.empty() && !f.help.empty()) dst->help = f.help;
      dst->metric.insert(dst->metric.end(), std::make_move_iterator(f.metric.begin()), std::make_move_iterator(f.metric.end()));
    }
  }
  for (const auto& w : ws) {
    auto text = HttpGetLocal(w);
    if (text.empty()) continue;
    auto fams = ParseTextExposition(text);
    for (auto& f : fams) {
      auto* dst = findFam(f.name, f.type);
      // Keep first non-empty help
      if (dst->help.empty() && !f.help.empty()) dst->help = f.help;
      // Append series
      dst->metric.insert(dst->metric.end(), std::make_move_iterator(f.metric.begin()), std::make_move_iterator(f.metric.end()));
    }
  }
  // 杩藉姞鑱氬悎瑙嗗浘锛坰um锛夛紝浠呴拡瀵?counter 涓?histogram锛涗繚鐣?per-component 鏄庣粏
  // 鎸?(labels - component) 杩涜姹囨€?
  auto labelKeyWithoutComponent = [](const std::vector<prometheus::ClientMetric::Label>& labs) {
    std::vector<prometheus::ClientMetric::Label> v;
    v.reserve(labs.size());
    for (const auto& l : labs) if (l.name != "component") v.push_back(l);
    std::sort(v.begin(), v.end());
    std::string key;
    for (size_t i=0;i<v.size();++i) {
      if (i) key.push_back('|');
      key.append(v[i].name); key.push_back('='); key.append(v[i].value);
    }
    return key;
  };

  auto ensureFam = [&](const std::string& name, prometheus::MetricType ty, const std::string& help) -> prometheus::MetricFamily& {
    for (auto& f : merged) if (f.name == name && f.type == ty) return f;
    merged.push_back({});
    merged.back().name = name; merged.back().type = ty; merged.back().help = help; return merged.back();
  };

  for (const auto& f : std::as_const(merged)) {
    if (f.type == prometheus::MetricType::Histogram) {
      // 鑱氬悎 histogram
      std::unordered_map<std::string, prometheus::ClientMetric> agg; // key -> aggregated metric
      for (const auto& m : f.metric) {
        auto key = labelKeyWithoutComponent(m.label);
        auto& dst = agg[key];
        if (dst.label.empty()) {
          // 鍒濆鍖栨爣绛撅紙鍘婚櫎 component锛?
          for (const auto& l : m.label) if (l.name != "component") dst.label.push_back(l);
        }
        dst.histogram.sample_count += m.histogram.sample_count;
        dst.histogram.sample_sum += m.histogram.sample_sum;
        // 鎸?upper_bound 姹囨€绘《
        for (const auto& b : m.histogram.bucket) {
          bool found=false;
          for (auto& db : dst.histogram.bucket) {
            if (db.upper_bound == b.upper_bound) { db.cumulative_count += b.cumulative_count; found=true; break; }
          }
          if (!found) {
            prometheus::ClientMetric::Bucket nb; nb.upper_bound = b.upper_bound; nb.cumulative_count = b.cumulative_count; dst.histogram.bucket.push_back(nb);
          }
        }
      }
      // 杈撳嚭鍒?merged锛坒amily 鍚屽悕锛屽悓绫诲瀷锛涗笉绉婚櫎鍘熸槑缁嗭級
      auto& outFam = ensureFam(f.name, f.type, f.help);
      for (auto& kv : agg) {
        auto m = std::move(kv.second);
        std::sort(m.histogram.bucket.begin(), m.histogram.bucket.end(), [](auto& a, auto& b){ return a.upper_bound < b.upper_bound; });
        outFam.metric.push_back(std::move(m));
      }
    } else if (f.type == prometheus::MetricType::Counter) {
      // 鑱氬悎 counter锛堟垨鎸?_total 瑙勫垯鐨?untyped锛?
      std::unordered_map<std::string, prometheus::ClientMetric> agg;
      for (const auto& m : f.metric) {
        auto key = labelKeyWithoutComponent(m.label);
        auto& dst = agg[key];
        if (dst.label.empty()) {
          for (const auto& l : m.label) if (l.name != "component") dst.label.push_back(l);
        }
        dst.counter.value += m.counter.value;
      }
      auto& outFam = ensureFam(f.name, f.type, f.help);
      for (auto& kv : agg) outFam.metric.push_back(std::move(kv.second));
    }
  }

  return merged;
}

} // namespace promkit::mux
