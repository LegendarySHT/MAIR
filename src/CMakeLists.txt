cmake_minimum_required(VERSION 3.13.4)

# include_directories(dfsan_rt)

# Use lld as linker
find_program(LLD_LINKER ld.lld)
if (LLD_LINKER)
    message(STATUS "Found ld.lld linker: ${LLD_LINKER}")
    add_link_options("-fuse-ld=lld")
else()
    message(STATUS "ld.lld linker not found, using default linker")
endif()


include_directories(include)


add_subdirectory(runtime)
add_subdirectory(compiler)
add_subdirectory(instrumentation)