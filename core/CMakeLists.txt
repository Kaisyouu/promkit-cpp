add_library(promkit-core STATIC)

target_sources(promkit-core
  PRIVATE
    ConfigToml.cpp
)

target_include_directories(promkit-core PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/core)

target_compile_features(promkit-core PUBLIC cxx_std_23)

# Link toml++ if available
if(TARGET tomlplusplus::tomlplusplus)
  target_link_libraries(promkit-core PUBLIC tomlplusplus::tomlplusplus)
endif()
