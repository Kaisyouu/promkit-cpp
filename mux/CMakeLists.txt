# Mux aggregator implementation (initial, per-proc merge only)
add_library(promkit-mux STATIC)
target_sources(promkit-mux
  PRIVATE
    MuxCollector.cpp
    TextParser.cpp
)
target_include_directories(promkit-mux
  PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/mux
    ${CMAKE_SOURCE_DIR}/3rd/prometheus-cpp/core/include
    ${CMAKE_BINARY_DIR}/3rd/prometheus-cpp/core/include
)
if(TARGET prometheus-cpp::core)
  target_link_libraries(promkit-mux PUBLIC promkit-core prometheus-cpp::core)
else()
  target_link_libraries(promkit-mux PUBLIC promkit-core)
endif()

if(WIN32)
  target_link_libraries(promkit-mux PUBLIC ws2_32)
  target_compile_definitions(promkit-mux PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()