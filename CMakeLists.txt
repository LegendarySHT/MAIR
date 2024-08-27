# Aligned with LLVM 15.0.7
cmake_minimum_required(VERSION 3.13.4)

# To compile *.S in runtime
enable_language(C CXX ASM)

project(xsan)

find_program(CLANG_C_COMPILER clang)
if (CLANG_C_COMPILER)
    set(CMAKE_C_COMPILER ${CLANG_C_COMPILER} CACHE STRING "C compiler" FORCE)
    set(CMAKE_CXX_COMPILER ${CLANG_C_COMPILER}++ CACHE STRING "C++ compiler" FORCE)
    message(STATUS "Clang found and set as default compiler: ${CLANG_C_COMPILER}")
else()
    message(STATUS "No Clang not found, using system default compiler (cc).")
endif()

message(STATUS "C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER}")

# export compile_commands.json for clangd
set( CMAKE_EXPORT_COMPILE_COMMANDS 1 )

# LLVM 15 requires C++14
set(CMAKE_CXX_STANDARD 14 CACHE STRING "C++ standard to conform to")

# Load LLVMConfig.cmake. If this fails, consider setting `LLVM_DIR` to point
# to your LLVM installation's `lib/cmake/llvm` directory.
find_package(LLVM REQUIRED CONFIG)
#add_definitions(${LLVM_DEFINITIONS})
#include_directories(${LLVM_INCLUDE_DIRS})
#link_directories(${LLVM_LIBRARY_DIRS})

# For AllSupportedArchDefs.cmake
list(INSERT CMAKE_MODULE_PATH 0
  "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/cmake/Modules"
)
include(AllSupportedArchDefs)
set(XSAN_SUPPORTED_ARCH ${X86_64})

message(STATUS "XSAN_SUPPORTED_ARCH: ${XSAN_SUPPORTED_ARCH}")

set(XSAN_BIN_DIR "${CMAKE_BINARY_DIR}")
set(XSAN_LIB_DIR "${CMAKE_BINARY_DIR}")
set(XSAN_PASS_DIR "${CMAKE_BINARY_DIR}/pass")
# TODO: de-hardcode this
set(XSAN_COMMON_LIT_CONFIGURE_PATH
${CMAKE_BINARY_DIR}/test/lit.common.configured)

add_subdirectory(src)
add_subdirectory(test)

