Third-Party Source (vendored)

- Place prometheus-cpp at: 3rd/prometheus-cpp (as a git submodule or copied source)
  * Required CMake options are set by top-level CMakeLists when present.
- CivetWeb can be brought by prometheus-cpp; separate 3rd/civetweb is optional.

Example (submodule):
  git submodule add https://github.com/jupp0r/prometheus-cpp 3rd/prometheus-cpp
  git submodule update --init --recursive
