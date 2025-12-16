cmake_minimum_required(VERSION 3.13)
project(MopIR_CompareIR)

find_package(LLVM REQUIRED CONFIG)

# LLVM
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Include paths for MopIR and Analysis
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../../Analysis)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../../Utils)

# Build MopIR static library from MopIR/lib
file(GLOB MOP_IR_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../../lib/*.cpp")
add_library(XSanMopIR STATIC ${MOP_IR_SOURCES})

# Analysis sources (Reducer)
set(ANALYSIS_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../Analysis/MopRecurrenceReducer.cpp
)
set(UTILS_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../Utils/Logging.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../Utils/Options.cpp
)

add_executable(CompareIR CompareIR.cpp ${ANALYSIS_SOURCES} ${UTILS_SOURCES})

llvm_map_components_to_libnames(llvm_libs
  Support
  Core
  IRReader
  Analysis
  ScalarOpts
  TransformUtils
  InstCombine
  IPO
  BitReader
  Passes
  Target
)

target_link_libraries(CompareIR ${llvm_libs} XSanMopIR)

# Workaround: reduce strictness for external headers included by Reducer
target_compile_options(CompareIR PRIVATE -fpermissive)

# Copy test IRs from MopIRTest folder
foreach(IRF test_simple.ll test_redundant.ll test_redundant_complex.ll test_recurrence.ll)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../MopIRTest/${IRF}
                 ${CMAKE_CURRENT_BINARY_DIR}/${IRF} COPYONLY)
endforeach()
