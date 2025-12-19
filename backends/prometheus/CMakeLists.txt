add_library(promkit-backend-prometheus STATIC)

target_sources(promkit-backend-prometheus
  PRIVATE
    PromBackend.cpp
)

target_include_directories(promkit-backend-prometheus PUBLIC ${CMAKE_SOURCE_DIR}/include)

if(TARGET prometheus-cpp::core)
  target_link_libraries(promkit-backend-prometheus PUBLIC prometheus-cpp::core prometheus-cpp::pull Threads::Threads)
  target_compile_definitions(promkit-backend-prometheus PUBLIC PROMKIT_BACKEND_PROM=1)
else()
  message(STATUS "prometheus-cpp target not found; promkit-backend-prometheus will be a stub; building empty library")
  target_compile_definitions(promkit-backend-prometheus PUBLIC PROMKIT_BACKEND_PROM_STUB=1)
endif()

target_compile_features(promkit-backend-prometheus PUBLIC cxx_std_23)
