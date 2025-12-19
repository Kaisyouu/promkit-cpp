#include <promkit/promkit.hpp>
#include <thread>
#include <chrono>
#include <iostream>

int main(int argc, char** argv) {
  using namespace std::chrono_literals;
  promkit::Config cfg;
  cfg.enabled = true;
  cfg.mode = "single";
  cfg.host = "127.0.0.1";
  cfg.port = 9464;
  cfg.path = "/metrics";
  cfg.prefix = "promkit";
  cfg.labels = { {"service","example"}, {"component","demo"}, {"env","dev"} };
  if (!promkit::Init(cfg)) {
    std::cerr << "Init failed" << std::endl;
    return 1;
  }

  auto c_ok = promkit::CreateCounter("orders_processed_total", "Total processed", {{"result","ok"}});
  auto c_err = promkit::CreateCounter("orders_processed_total", "Total processed", {{"result","error"}});
  auto g_backlog = promkit::CreateGauge("order_backlog", "Pending queue length");
  auto h_latency = promkit::CreateHistogram("order_processing_seconds", "Latency", {0.001,0.005,0.01,0.05,0.1,0.25,0.5,1});

  std::cout << "Serving metrics on http://" << cfg.host << ":" << cfg.port << cfg.path << std::endl;
  for (int i = 0; i < 20; ++i) {
    promkit::CounterAdd(c_ok, 1);
    if (i % 5 == 0) promkit::CounterAdd(c_err, 1);
    promkit::GaugeSet(g_backlog, 100 - i);
    {
      promkit::ScopeTimer t(h_latency);
      std::this_thread::sleep_for(5ms + std::chrono::milliseconds(i % 7));
    }
    std::this_thread::sleep_for(200ms);
  }

  promkit::Shutdown();
  return 0;
}

