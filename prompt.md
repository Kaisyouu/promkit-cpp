promkit-cpp: A Thin, Config-Driven Prometheus Kit with Single-Port Multiprocess Aggregation

Intent
- Provide a minimal, reusable C++23 wrapper over prometheus-cpp that standardizes metric naming, labels, buckets, and lifecycle.
- Support two deployment modes with the same API: (1) single-process /metrics; (2) multi-process on the same host sharing one port via an in-host aggregator (“mux”).
- Keep disabled overhead near-zero through a Noop backend and optional compile-time disable.
- Build from source only (3rd/ submodules), targeting Ubuntu (gcc/clang) and Windows (MSVC 19.50, SDK 10.0.26100.0).

Non-Goals
- Do not implement a generic dynamic metric system where arbitrary names/labels appear at runtime. We keep metric shape mostly static and configuration-scoped.
- Do not hot-reload metric definitions. Config is loaded once at start; only a subset of runtime flags may change in the future (not in v1).
- Do not support PushGateway in v1.

Core Ideas
- Config declares exporter settings, global labels, bucket profiles, and metric families (type/name/help/unit/labels). Observation in code uses fast handles, not string lookups.
- Noop implementation when disabled: API calls compile away to a tiny branch or macro; no registry, no HTTP server.
- Multiprocess “mux” mode: exactly one HTTP port per host per service component. Other processes expose to the mux via ephemeral ports and register themselves, or the mux scrapes them and merges.

Architecture Overview
1) Library Layers
- api/: Handle-centric API: get counter/gauge/histogram handles; RAII scope timer for latency; optional per-enum label variants.
- core/: Registry/Exposer manager, config loader, pre-registration of families and series, bucket profiles, common label injection.
- backends/:
  - prometheus: uses prometheus-cpp + CivetWeb for HTTP.
  - noop: zero-op drop-in backend when disabled or in tests.
- mux/: Aggregator server and worker registration client. Responsible for single-port exposure when multiple processes run on the same host.

2) Modes
- single: Each process starts its own HTTP exposer on configured host:port/path.
- mux: One process per host (or per component) binds the configured port and becomes the aggregator. Other processes:
  - Start a local HTTP exposer on an ephemeral port (bind port=0 to 127.0.0.1).
  - Register themselves to the aggregator over localhost (POST /_internal/register) with their endpoint info and metadata (pid, process_name, component, proc=logical name).
  - On shutdown, deregister (/_internal/deregister). The aggregator also prunes dead workers on failure/timeouts.

3) Scrape Flow in mux mode
- Prometheus scrapes aggregator: GET /metrics.
- Aggregator concurrently scrapes all registered workers (localhost:ephemeral/metrics) with a strict timeout and fan-in.
- Aggregator merges results and responds. Merge behavior is configurable; default is both:
  - sum_only: Only publish aggregated series (no proc label).
  - per_proc: Publish each worker’s series with extra labels proc (logical name, required) and pid, no aggregation.
  - both: Publish both aggregated and per_proc series (default).
- Histograms: per bucket `_bucket` counts are summed; `_sum` and `_count` are summed. Summaries are discouraged (not aggregatable) and disabled in mux by default.

4) Registration and Discovery
- Workers discover aggregator by attempting to bind the configured port:
  - Success → becomes the aggregator.
  - Failure (EADDRINUSE) → assumes a mux is active; binds an ephemeral port for its own /metrics and registers to the mux.
- Registration payload: { endpoint: "127.0.0.1:ephemeral", pid, process_name, component, version, proc }.
- Security: by default, accept only localhost registrations; optional shared token.
- Validation: proc (business logical name) is mandatory; missing proc yields 400 and registration is rejected.

5) Failure Handling
- If a worker is unresponsive during mux scrape, aggregator omits its data for that scrape and exposes a self-metric (dropped_workers_total, scrape_duration_seconds for fanout, workers_active).
- If aggregator dies, the next process that starts (or restarts) and can bind the configured port becomes the aggregator. Workers periodically re-register if their healthcheck to aggregator fails.

Decisions (confirmed)
- mux publish default: both (aggregated and per-process output at the same time).
- proc label: must be provided by each process at registration; missing proc is a hard error.
- component label is sufficient; no extra app label.
- Gauges: need per-process distinction; when publish=both, also output an aggregated value (default aggregation=sum; can be overridden per-metric to last/max).

Public API (shape, not code)
- Init(config_path): loads config, sets global labels, pre-registers metric families and timeseries, starts exposer (single or mux mode auto-election).
- GetCounter(name[, static_labels or label_variant_id]) -> CounterHandle (methods: inc(), add(double)).
- GetGauge(name[,...]) -> GaugeHandle (set(double), inc(), dec()).
- GetHistogram(name[,...]) -> HistogramHandle (observe(double)).
- ScopeTimer(name[,...]) -> RAII object that observes on destruction; or timer.ObserveAndReset().
- Shutdown(): graceful stop of exposer/mux, flushes, deregisters if worker.

Labeling Strategy
- Global labels injected automatically: service, env, version, instance (hostname or configured), component.
- Dynamic labels restricted to low-cardinality enumerations defined in config (e.g., result=[ok,error], stage=[parse,validate,...]).
- Per-process labels in mux mode: proc (business logical name, required) and pid (auto attached). These appear on per-proc series when publish=per_proc or both.

Configuration (TOML example; loaded once)

[exporter]
mode = "mux"                # "single" | "mux"
enabled = true
host = "0.0.0.0"
port = 9464                 # single well-known port per host/component
path = "/metrics"
namespace = "promkit"
prefix = ""
# If mux
[mux]
registration_path = "/_internal/register"
deregistration_path = "/_internal/deregister"
scrape_timeout_ms = 200
fanout_concurrency = 16
publish = "both"           # default for mux: "sum_only" | "per_proc" | "both"
require_localhost = true
shared_token = ""           # optional; empty disables

[labels]
service = "order-gateway"
component = "csv-worker"
env = "prod"
version = "1.2.3"
instance = "${HOSTNAME}"
proc = "order_reader_a"      # required: business logical name per process

[buckets]
# Named profiles reused by metrics
latency_short = [0.001, 0.005, 0.01, 0.05, 0.1, 0.25, 0.5, 1, 2]
latency_long  = [0.01, 0.05, 0.1, 0.25, 0.5, 1, 2, 5, 10]

[[metrics]]
name = "orders_received_total"
type = "counter"
help = "Total orders received"
unit = "events"
const_labels = { source = "csv" }
publish = "both"                # inherit mux.publish by default; explicit for clarity

[[metrics]]
name = "orders_processed_total"
type = "counter"
help = "Total orders processed"
unit = "events"
dynamic_labels = { result = ["ok", "error"] }
publish = "both"

[[metrics]]
name = "order_processing_seconds"
type = "histogram"
help = "Order processing latency"
unit = "seconds"
buckets_profile = "latency_short"
dynamic_labels = { stage = ["parse", "validate", "write", "respond"] }
publish = "both"

[[metrics]]
name = "csv_watch_events_total"
type = "counter"
help = "CSV watcher events"
unit = "events"
dynamic_labels = { type = ["create", "modify", "error"] }
publish = "both"


Merging Rules (mux mode)
- Counter: aggregated view sums across workers for the same series (excluding proc/pid); per-proc view appends proc/pid.
- Gauge: default publish per-proc; when publish=both, also output an aggregated value with aggregation strategy (default=sum; configurable per metric to last/max).
- Histogram: sum per-le bucket; also sum _sum and _count. Types validated at registration. Summaries are unsupported in mux mode.

Performance Guidance
- Pre-register all series from config (cartesian product of dynamic label enums). Observation paths avoid string maps; handles point to concrete prometheus-cpp objects.
- Limit buckets to <= 12 per histogram to control memory and scrape size.
- Avoid high-cardinality labels. Enumerate allowed values in config.
- Noop backend when exporter.enabled=false: handle methods inline to no-ops; RAII timer is an empty object.

Vendor Layout and CMake Framework

Third-party (source only; as submodules under 3rd/)
- 3rd/prometheus-cpp    # official repo; build with pull (CivetWeb) enabled
- 3rd/civetweb          # optional if not bundled; prometheus-cpp can fetch it, but we vendor for determinism

Repository Layout (refined)
- /api        public headers (thin API, handles, RAII timer)
- /core       registry/exposer manager, config loader, buckets
- /backends   prometheus (glue to prometheus-cpp), noop backend
- /mux        aggregator server, worker client, merge engine, text parser
- /3rd        prometheus-cpp, civetweb (git submodules)
- /cmake      toolchain and helper modules
- /examples   minimal demos (disabled by default)
- /tests      unit tests (later)

CMake Top-Level (concept)
```
cmake_minimum_required(VERSION 3.22)
project(promkit-cpp VERSION 0.1.0 LANGUAGES C CXX)

option(PROMKIT_BUILD_MUX "Build mux aggregator" ON)
option(PROMKIT_ENABLE_TLS "Enable TLS for /metrics" OFF)
option(PROMKIT_BUILD_TESTS "Build tests" OFF)
option(PROMKIT_VENDOR_TP "Use vendored third-party" ON)
option(PROMKIT_BUILD_SHARED "Build shared libraries" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Threads REQUIRED)

if(PROMKIT_VENDOR_TP)
  # prometheus-cpp options
  set(ENABLE_PULL  ON  CACHE BOOL "" FORCE)
  set(ENABLE_PUSH  OFF CACHE BOOL "" FORCE)
  set(ENABLE_COMPRESSION OFF CACHE BOOL "" FORCE)
  set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(OVERRIDE_CIVETWEB TRUE CACHE BOOL "" FORCE)
  add_subdirectory(3rd/prometheus-cpp)
  # civetweb may be pulled by prometheus-cpp; 3rd/civetweb is optional
endif()

add_subdirectory(core)
add_subdirectory(api)
add_subdirectory(backends/prometheus)
add_subdirectory(backends/noop)
if(PROMKIT_BUILD_MUX)
  add_subdirectory(mux)
endif()

include(GNUInstallDirs)
install(DIRECTORY api/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promkit-cpp)
```

Targets (proposed)
- promkit-core       (STATIC/SHARED): core registry/config/buckets
- promkit-backend    (INTERFACE): selects one of backends via compile defs
- promkit-backend-prometheus (STATIC): links to prometheus-cpp, civetweb, Threads
- promkit-backend-noop        (HEADER-ONLY or STATIC)
- promkit-mux        (STATIC/SHARED, optional): aggregator server + worker client
- promkit            (INTERFACE): umbrella target exporting include dirs and link to the chosen backend + core (+ mux when enabled)

Windows toolchain notes
- MSVC 19.50, SDK 10.0.26100.0. Expose option PROMKIT_MSVC_RUNTIME: "dynamic" (/MD) or "static" (/MT). Example:
```
if(MSVC)
  set(PROMKIT_MSVC_RUNTIME "dynamic" CACHE STRING "MSVC runtime linkage")
  if(PROMKIT_MSVC_RUNTIME STREQUAL "static")
    foreach(flag_var CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_DEBUG)
      string(REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
    endforeach()
  endif()
endif()
```

Linking
- Linux: link Threads::Threads; optionally -static-libstdc++/-static-libgcc only when requested.
- Windows: link ws2_32, psapi when needed by civetweb/prometheus-cpp.

Subdir CMake skeleton (example: backends/prometheus/CMakeLists.txt)
```
add_library(promkit-backend-prometheus)
target_sources(promkit-backend-prometheus
  PRIVATE
    PromBackend.cpp
    PromSerializer.cpp
)
target_include_directories(promkit-backend-prometheus PUBLIC ${CMAKE_SOURCE_DIR}/api)
target_link_libraries(promkit-backend-prometheus PUBLIC prometheus-cpp::core prometheus-cpp::pull Threads::Threads)
target_compile_features(promkit-backend-prometheus PUBLIC cxx_std_23)
```

Mux build (mux/CMakeLists.txt)
```
add_library(promkit-mux)
target_sources(promkit-mux PRIVATE MuxServer.cpp MuxMerge.cpp MuxClient.cpp TextParser.cpp)
target_link_libraries(promkit-mux PUBLIC promkit-core promkit-backend-prometheus)
```

Usage in apps
```
find_package(promkit-cpp CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE promkit)
```

Self-Metrics
- promkit_mux_workers{state="active|dropped"}
- promkit_mux_fanout_duration_seconds (histogram)
- promkit_mux_scrape_failures_total
- promkit_mux_publish_mode (gauge as enum or info metric)

Limitations (v1)
- Summary metrics not supported in mux mode.
- Hot reload of metric definitions not supported; change requires restart.
- Aggregator merges only recognized metric types; custom collectors must output Prometheus text/OM compatible.
- Default publish=both increases time-series count and scrape traffic; capacity-plan Prometheus/TSDB accordingly.

Roadmap
- v0.1 (MVP): single mode + basic API + config + RAII timer + Noop backend.
- v0.2: mux mode with registration and fanout scrape; counter/gauge/histogram merge; self-metrics.
- v0.3: TLS for /metrics, token for registration; buckets tuning; examples and dashboards.
- v0.4: optional compile-time disable, more backends hygiene, Windows service helper.

Open Questions (later)
- Do we need TLS in early versions, or keep it off until v0.3?
- Do we need an optional registration token from day one, or add it when exposing beyond localhost?
- For special gauges, do we need max/last aggregation presets besides sum?
