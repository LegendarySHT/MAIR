cmake_minimum_required(VERSION 3.13.4)

# include_directories(dfsan_rt)

set(XSAN_BIN_DIR "${CMAKE_BINARY_DIR}")
set(XSAN_LIB_DIR "${CMAKE_BINARY_DIR}")
set(XSAN_PASS_DIR "${CMAKE_BINARY_DIR}/pass")

add_subdirectory(runtime)
add_subdirectory(compiler)
add_subdirectory(instrumentation)