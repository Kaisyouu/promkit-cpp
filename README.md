# promkit-cpp

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j

## Config Guidelines: Single vs Mux

promkit-cpp 支持两种运行模式：
- single：每个进程独立暴露一个 /metrics 端口；Prometheus 直接抓取每个进程。
- mux：同一台机器仅暴露一个 /metrics 端口（聚合器）；其他进程作为 worker 暴露本地临时端口，由聚合器抓取后合并再对外暴露。

公共字段（两种模式通用）
- exporter.host / exporter.port / exporter.path：HTTP 暴露地址/端口/路径。
- exporter.namespace：指标名前缀（如 `oms` → `oms_orders_total`）。建议一组服务内保持一致。
- labels 全局标签（注入到所有指标）：`service`/`component`/`env`/`version`/`instance`。
- buckets：直方图桶配置，需在所有涉及该 profile 的进程里保持一致。
- metrics：尽量在配置中预定义指标族（type/name/help/buckets_profile），避免运行时形状漂移；动态标签请用枚举方式声明允许值。

Single 模式原则
- 每个进程独立绑定 exporter.host:port:path；避免冲突（每进程端口不同）。
- `labels.instance` 建议唯一标识该 scrape 目标（常用 `host:port` 或主机名）；PromQL 中可以用 `sum by(instance)` 维度来区分。
- `labels.component` 可用于标注进程角色（如 A/B、reader/writer）。
- 同一服务的 `namespace`/`service`/`env`/`version` 在不同进程间应保持一致，便于聚合与筛选。

Mux 模式原则（单端口多进程）
- 选主：谁先绑定配置端口谁就是聚合器；其他进程自动降级为 worker（绑定 127.0.0.1 的临时端口并注册给聚合器）。
- 目录发现：worker 在 `/tmp/promkit-mux/<namespace>` 写入自身端点描述；聚合器本地抓取并合并。
- 必填标签：`labels.component` 必须为每个进程设置不同的值（用来区分不同 trader/worker）。
- `labels.instance`：
  - 推荐在 mux 模式下设置为“相同值”，代表聚合器对外的 scrape 目标（如 `oms-agg.local` 或 `127.0.0.1:9464`）。
  - 聚合视图（sum）是“在移除 component 维度后”进行的求和；为了让不同进程的数据汇总到一起，其他全局标签（尤其 `instance`）也应保持一致，否则会出现按 `instance` 切分而无法合并的情况。
  - 结论：在 mux 下，A/B 两个进程的 `instance` 请配置为相同值；或直接省略该标签。
- 其他标签：`service`/`env`/`version` 建议在所有进程保持一致。（它们影响汇总的键，若不同将导致无法聚合到一条总量时序。）
- 聚合输出策略（当前默认）：both
  - 明细视图（per-component）：每个进程一条时序，带 `component=<进程名>`。
  - 聚合视图（sum）：移除 `component` 后求和，输出总量时序（不带 `component`）。
- 直方图合并：对每个 `*_bucket`、`*_sum`、`*_count` 求和；务必确保所有进程采用相同桶配置（同一个 `buckets_profile`）。

常见问题（FAQ）
- 问：在 mux 模式下 `instance` 可以填一样的吗？
  - 答：不仅可以，而且推荐填一样（或干脆不填）。这样聚合视图才能把不同进程的数据真正汇总成一条总量时序；否则会被不同的 `instance` 分裂成多条时序。
- 问：需要第三个“专门的聚合器”进程吗？
  - 答：不需要。两进程场景下，谁先绑定端口谁当聚合器；另一个自动作为 worker。
- 问：聚合器自己的指标会被合并吗？
  - 答：会。聚合器也按自身 `labels.component` 作为一个“worker”参与 per-component 与 sum 的合并。

PromQL 参考
- 明细吞吐：`sum by(component) (rate(<ns>_orders_received_total[1m]))`
- 总量吞吐：`sum(rate(<ns>_orders_received_total[1m]))`
- 明细延迟 P95：`histogram_quantile(0.95, sum by (le, component) (rate(<ns>_order_processing_seconds_bucket[5m])))`
- 总量延迟 P95：`histogram_quantile(0.95, sum by (le) (rate(<ns>_order_processing_seconds_bucket[5m])))`

示例
- 单进程（single）：`examples/configs/oms_trader.toml`
- Mux 多进程（两个 trader）：
  - A：`examples/configs/oms_trader_A.toml`（component=`test_promkit_trader_A`）
  - B：`examples/configs/oms_trader_B.toml`（component=`test_promkit_trader_B`）
  - 启动两个 `example_oms_trader` 进程即可复现；访问聚合器端口查看总量与明细。

注意事项
- 指标形状需一致：名称、类型、桶定义、允许的动态标签集合在各进程需一致，避免合并失败或出现不期望的高基数。
- 性能与稳定性：避免高频新增时序；限制直方图桶数量（建议 <= 12）；worker 与聚合器在同一台机器（只抓取本地回环地址）。


## Windows (VS2026 / MT, MTd)

- 依赖：Visual Studio 2026（或 2022）、CMake >= 3.22、Windows 10 SDK。
- 克隆并拉取子模块（toml++ 必须；prometheus-cpp 可选）：
  ```powershell
  git clone https://github.com/your-org/promkit-cpp.git
  cd promkit-cpp
  git submodule update --init 3rd/tomlplusplus
  # 可选：git submodule update --init 3rd/prometheus-cpp
  ```
- 生成工程（多配置生成器，x64）。项目默认使用静态运行时（Release=/MT, Debug=/MTd），库为静态：
  ```powershell
  cmake -S . -B build-msvc -G "Visual Studio 18 2026" -A x64
  # 如用 VS2022：-G "Visual Studio 17 2022"
  ```
- 编译：
  ```powershell
  cmake --build build-msvc --config Debug   --parallel
  cmake --build build-msvc --config Release --parallel
  ```
- 产物：
  - build-msvc/core/<Config>/promkit-core.lib
  - build-msvc/backends/noop/<Config>/promkit-backend-noop.lib
  - build-msvc/examples/<Config>/*.exe
- 说明：
  - mux 在 Windows 上已启用（使用 Winsock）；worker 描述目录使用 std::filesystem::temp_directory_path()。
  - 如需 PowerShell 7，可将 post-build 调用改为 `pwsh.exe`。
