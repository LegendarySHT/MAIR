cmake_minimum_required(VERSION 3.13.4)

# include_directories(dfsan_rt)

include_directories(include)

add_subdirectory(runtime)
add_subdirectory(compiler)
add_subdirectory(instrumentation)