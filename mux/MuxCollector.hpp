// MuxCollector: a Collectable that scrapes multiple worker endpoints and merges
#pragma once

#include <prometheus/collectable.h>
#include <prometheus/metric_family.h>

#include <mutex>
#include <memory>
#include <string>
#include <vector>

namespace prometheus { class Registry; }

namespace promkit::mux {

struct WorkerEndpoint {
  std::string host;    // 127.0.0.1
  int         port;    // ephemeral
  std::string path;    // /metrics
  // labels to inject for per-proc view
  std::string component;    // component distinguisher (from labels.component)
  int         pid = 0; // kept for future debugging; not exported as label
};

// Very small HTTP getter using civetweb client API would be ideal, but to keep
// this minimal and portable within current repo, we will delegate the actual
// fetching to the exposer server by using prometheus-cpp primitives is not possible.
// Here we only declare the interface. Implementation uses a naive TCP fetch (optional).
class MuxCollector : public prometheus::Collectable {
 public:
  MuxCollector() = default;
  // Directory to scan worker descriptors, e.g. /tmp/promkit-mux/ns_component
  void SetDirectory(std::string dir);
  // Optional static workers set (tests). When non-empty, directory is ignored.
  void SetWorkers(std::vector<WorkerEndpoint> workers);
  // Include aggregator's own registry into per-component merge
  void SetSelf(std::shared_ptr<prometheus::Registry> self, std::string component);
  std::vector<prometheus::MetricFamily> Collect() const override;

 private:
  std::vector<WorkerEndpoint> workers_;
  std::string dir_;
  std::weak_ptr<prometheus::Registry> self_;
  std::string self_component_;
};

} // namespace promkit::mux
