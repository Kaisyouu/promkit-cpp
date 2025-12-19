# Header-only noop backend placeholder
add_library(promkit-backend-noop INTERFACE)
target_include_directories(promkit-backend-noop INTERFACE ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(promkit-backend-noop INTERFACE PROMKIT_BACKEND_NOOP=1)
