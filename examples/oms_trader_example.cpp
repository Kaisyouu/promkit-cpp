// Simple OMS trader example
// - A background thread simulates an upstream system sending orders
// - We record two metrics:
//     1) orders_received_total (counter)
//     2) order_processing_seconds (histogram)

#include <promkit/promkit.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <random>
#include <string>
#include <thread>

struct OrderStruct {
    int64_t order_no;
    char symbol[16];
};

// Metric ids (created in main after InitFromToml)
static promkit::CounterId g_orders_received = 0;
static promkit::HistogramId g_order_proc_hist = 0;

// Simulate order processing with random latency and record it via histogram
static void processOrder(const OrderStruct& /*order*/)
{
    using namespace std::chrono_literals;
    promkit::ScopeTimer t(g_order_proc_hist); // observes seconds on destruction

    // Random sleep 2-50ms to mimic processing
    static thread_local std::mt19937 rng{std::random_device{}()};
    static thread_local std::uniform_int_distribution<int> dist_ms(2, 50);
    std::this_thread::sleep_for(std::chrono::milliseconds(dist_ms(rng)));
}

// Order callback: count the order and time the processing
static void onOrder(const OrderStruct& order)
{
    (void)order; // not used in this demo
    promkit::CounterAdd(g_orders_received, 1.0);
    processOrder(order);
}

// Start a mock order source thread. It emits `count` orders spaced randomly.
static std::thread start_mock_order_source(std::function<void(const OrderStruct&)> cb,
                                           int count,
                                           int min_gap_ms = 10,
                                           int max_gap_ms = 60)
{
    return std::thread([cb, count, min_gap_ms, max_gap_ms]{
        static thread_local std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<int> dist_gap(min_gap_ms, max_gap_ms);
        OrderStruct o{};
        std::strncpy(o.symbol, "TEST", sizeof(o.symbol)-1);
        if (count <= 0) {
            int64_t i = 0;
            while (true) {
                o.order_no = ++i;
                cb(o);
                std::this_thread::sleep_for(std::chrono::milliseconds(dist_gap(rng)));
            }
        } else {
            for (int i = 0; i < count; ++i) {
                o.order_no = i + 1;
                cb(o);
                std::this_thread::sleep_for(std::chrono::milliseconds(dist_gap(rng)));
            }
        }
    });
}

int main(int argc, char** argv)
{
    // Config path can be provided as argv[1]; default to examples/configs/oms_trader.toml
    const std::string cfg_path = (argc > 1) ? argv[1] : std::string{"examples/configs/oms_trader.toml"};
    if (!promkit::InitFromToml(cfg_path)) {
        std::cerr << "InitFromToml failed: " << cfg_path << std::endl;
        return 1;
    }

    // Create metric handles. These names must match the TOML definitions.
    g_orders_received = promkit::CreateCounter("orders_received_total", "Total number of received orders");
    g_order_proc_hist = promkit::CreateHistogram("order_processing_seconds", "Order processing latency", {});

    // Start mock thread to emit infinite orders (Ctrl+C to stop). 如果只想跑固定次数，改第二个参数为正数即可。
    auto t = start_mock_order_source(onOrder, 0);

    // Keep process alive until thread completes; metrics served at configured /metrics
    t.join();

    promkit::Shutdown();
    // 容错：如果还有后台线程没完全退出，可按需查询运行状态
    // if (!promkit::IsRunning()) { /* safe to exit */ }
    return 0;
}
