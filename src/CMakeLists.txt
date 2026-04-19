cmake_minimum_required(VERSION 3.13.4)

# include_directories(dfsan_rt)

include_directories(include)

# 必须在 add_subdirectory(runtime) 之前声明：runtime 会拉取 test/asan，lit 需读取这些开关。
option(MAIR_USE_MOPIR_ASAN_REDUNDANCY
  "Use MopIRImplementation (RedundantCheckEliminationPass) for ASan recurrence reduction"
  OFF)
option(MAIR_USE_MOPIR_ASAN_LOOP_RELOC
  "Enable MopIR loop invariant relocation; check-asan lit 会设置 MAIR_MOPIR_LOOP_RELOC_TEST=1"
  OFF)

add_subdirectory(runtime)
add_subdirectory(compiler)
add_subdirectory(instrumentation)