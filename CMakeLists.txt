# Aligned with LLVM 20
cmake_minimum_required(VERSION 3.20.0)

project(xsan)
# To compile *.S in runtime
enable_language(C CXX ASM)
# export compile_commands.json for clangd
set( CMAKE_EXPORT_COMPILE_COMMANDS 1 )


if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(FATAL_ERROR "
No build type selected. You need to pass -DCMAKE_BUILD_TYPE=<type> in order to configure LLVM.
Available options are:
  * -DCMAKE_BUILD_TYPE=Release - For an optimized build with no assertions or debug info.
  * -DCMAKE_BUILD_TYPE=Debug - For an unoptimized build with assertions and debug info.
  * -DCMAKE_BUILD_TYPE=RelWithDebInfo - For an optimized build with no assertions but with debug info.
  * -DCMAKE_BUILD_TYPE=MinSizeRel - For a build optimized for size instead of speed.
Learn more about these options in our documentation at https://llvm.org/docs/CMake.html#cmake-build-type
")
endif()

# Check the type if one of the four supported build types
if(NOT CMAKE_BUILD_TYPE MATCHES "^(Release|Debug|RelWithDebInfo|MinSizeRel)$")
  message(FATAL_ERROR "
Invalid build type: ${CMAKE_BUILD_TYPE}. You need to pass -DCMAKE_BUILD_TYPE=<type> in order to configure LLVM.
Available options are:
  * -DCMAKE_BUILD_TYPE=Release - For an optimized build with no assertions or debug info.
  * -DCMAKE_BUILD_TYPE=Debug - For an unoptimized build with assertions and debug info.
  * -DCMAKE_BUILD_TYPE=RelWithDebInfo - For an optimized build with no assertions but with debug info.
  * -DCMAKE_BUILD_TYPE=MinSizeRel - For a build optimized for size instead of speed.
Learn more about these options in our documentation at https://llvm.org/docs/CMake.html#cmake-build-type
")
endif()

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


# LLVM 15 requires C++14, but we only compile the runtime and instrumentation
# supprot C++17
set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard to conform to")

# Load LLVMConfig.cmake. If this fails, consider setting `LLVM_DIR` to point
# to your LLVM installation's `lib/cmake/llvm` directory.
find_package(LLVM REQUIRED CONFIG)
#add_definitions(${LLVM_DEFINITIONS})
#include_directories(${LLVM_INCLUDE_DIRS})
#link_directories(${LLVM_LIBRARY_DIRS})

# To find *.cmake
list(INSERT CMAKE_MODULE_PATH 0
  "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/cmake"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/cmake/Modules"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/cmake"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/cmake/Modules"
)

include(Message) # introduce message_green()... function
include(AllSupportedArchDefs)

# Notify user to specify the LLVM_PATH, otherwise we cannot
# enable the full support of XSAN.
# LLVM_PATH could be set by environment variable or CMake variable.
if(NOT LLVM_PATH)
  message(WARNING "LLVM_PATH is not set. Please set it to your LLVM source directory.")
else()
  include(HandleOutOfTreeLLVM)
  set(LLVM_THIRD_PARTY_DIR ${LLVM_PATH}/third-party)
endif()




# Now we only support x86_64
set(XSAN_SUPPORTED_ARCH ${X86_64})
message_green("XSAN_SUPPORTED_ARCH: ${XSAN_SUPPORTED_ARCH}")

set(XSAN_BIN_DIR "${CMAKE_BINARY_DIR}")
set(XSAN_LIB_DIR "${CMAKE_BINARY_DIR}")
set(XSAN_RESOURCE_DIR "${CMAKE_BINARY_DIR}/share")
set(XSAN_PASS_DIR "${CMAKE_BINARY_DIR}/pass")
# TODO: de-hardcode this
set(XSAN_COMMON_LIT_CONFIGURE_PATH
  ${CMAKE_BINARY_DIR}/test/lit.common.configured)

add_subdirectory(src)
# Now this directory is added from /src/runtime/test/CMakeLists.txt, 
# in order to include the relevant variables.
# add_subdirectory(test)

