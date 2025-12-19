# Placeholder: core is header-only for now
add_library(promkit-core-impl INTERFACE)
target_include_directories(promkit-core-impl INTERFACE ${CMAKE_SOURCE_DIR}/include)
