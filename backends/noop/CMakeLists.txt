add_library(promkit-backend-noop STATIC)

target_sources(promkit-backend-noop
  PRIVATE
    NoopBackend.cpp
)

target_include_directories(promkit-backend-noop PUBLIC ${CMAKE_SOURCE_DIR}/include)

target_compile_definitions(promkit-backend-noop PUBLIC PROMKIT_BACKEND_NOOP=1)

target_compile_features(promkit-backend-noop PUBLIC cxx_std_23)
