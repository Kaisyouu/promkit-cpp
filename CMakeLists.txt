cmake_minimum_required(VERSION 3.22)
project(promkit-cpp VERSION 0.1.0 LANGUAGES C CXX)

# Options
option(PROMKIT_BUILD_MUX "Build mux aggregator (single-port multiprocess)" ON)
option(PROMKIT_ENABLE_TLS "Enable TLS for /metrics (future)" OFF)
option(PROMKIT_BUILD_TESTS "Build tests" OFF)
option(PROMKIT_VENDOR_TP "Use vendored third-party under 3rd/" ON)
option(PROMKIT_BUILD_SHARED "Build shared libraries" OFF)
option(PROMKIT_USE_PROM_BACKEND "Enable prometheus-cpp backend when available" ON)

# Language & common
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Threads REQUIRED)

# Third-party (optional, source-only)
# We'll only add_subdirectory if 3rd/prometheus-cpp exists.
if(PROMKIT_VENDOR_TP)
  if(EXISTS ${CMAKE_SOURCE_DIR}/3rd/prometheus-cpp/CMakeLists.txt)
    # prometheus-cpp configuration
    set(ENABLE_PULL  ON  CACHE BOOL "" FORCE)
    set(ENABLE_PUSH  OFF CACHE BOOL "" FORCE)
    set(ENABLE_COMPRESSION OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(OVERRIDE_CIVETWEB TRUE CACHE BOOL "" FORCE)
    add_subdirectory(3rd/prometheus-cpp)
  # header-only config lib
  if(EXISTS ${CMAKE_SOURCE_DIR}/3rd/tomlplusplus/CMakeLists.txt)
    add_subdirectory(3rd/tomlplusplus)
  endif()
  else()
    message(STATUS "prometheus-cpp not present under 3rd/; prom backend will be stubbed (noop)")
  endif()
endif()

# Subdirectories
add_subdirectory(core)
add_subdirectory(backends/noop)
add_subdirectory(backends/prometheus)
if(PROMKIT_BUILD_MUX)
  add_subdirectory(mux)
endif()

# Umbrella target
add_library(promkit INTERFACE)
# Always include public headers
set(PROMKIT_PUBLIC_INCLUDE ${CMAKE_SOURCE_DIR}/include)

target_include_directories(promkit INTERFACE ${PROMKIT_PUBLIC_INCLUDE})

# Core is header-only for now
add_library(promkit-core INTERFACE)
target_include_directories(promkit-core INTERFACE ${PROMKIT_PUBLIC_INCLUDE})

# Backend selection (prometheus if available and enabled; otherwise noop)
set(_PROMKIT_BACKEND "noop")
if(PROMKIT_USE_PROM_BACKEND AND TARGET prometheus-cpp::core)
  set(_PROMKIT_BACKEND "prometheus")
endif()

if(_PROMKIT_BACKEND STREQUAL "prometheus")
  target_link_libraries(promkit INTERFACE promkit-core promkit-backend-prometheus Threads::Threads)
else()
  target_link_libraries(promkit INTERFACE promkit-core promkit-backend-noop)
endif()

if(PROMKIT_BUILD_MUX)
  target_link_libraries(promkit INTERFACE promkit-mux)
endif()

include(GNUInstallDirs)
# Install public headers under include/promkit-*
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/)
