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

add_subdirectory(src)

