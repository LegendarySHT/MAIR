# Include the part of LLVM's CMake libraries that defines
# `add_llvm_pass_plugin`.
include(AddLLVM)

# Use LLVM's preprocessor definitions, include directories, and library search
# paths.
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})

# -Wl,-Bsymbolic : force local symbols to be used in shared library
add_link_options(-Wl,-Bsymbolic)

set(XSAN_PASS_COMMON
  AttributeTaggingPass.cpp
  Instrumentation.cpp
  PassRegistry.cpp
  UbsanInstTagging.cpp
  Analysis/ActiveMopAnalysis.cpp
  Analysis/MopRecurrenceReducer.cpp
  Analysis/TsanMopAnalysis.cpp
  Utils/ValueUtils.cpp
  Utils/Logging.cpp
  Utils/MetaDataUtils.cpp
  Utils/Options.cpp
  Utils/UbsanUtils.cpp
)

# TODO: all pass into 1 so
add_llvm_pass_plugin(ASanInstPass
  PARTIAL_SOURCES_INTENDED # Skip checking for else files
  AddressSanitizer.cpp
  ${XSAN_PASS_COMMON}
)

add_llvm_pass_plugin(TSanInstPass
  PARTIAL_SOURCES_INTENDED # Skip checking for else files
  ThreadSanitizer.cpp
  ${XSAN_PASS_COMMON}
)

add_llvm_pass_plugin(CovSanInstPass
  PARTIAL_SOURCES_INTENDED # Skip checking for else files
  SanitizerCoverage.cpp
)

add_llvm_pass_plugin(MSanInstPass
  PARTIAL_SOURCES_INTENDED # Skip checking for else files
  MemorySanitizer.cpp
  ${XSAN_PASS_COMMON}
)

add_llvm_pass_plugin(XSanInstPass
  PARTIAL_SOURCES_INTENDED # Skip checking for else files
  ThreadSanitizer.cpp
  AddressSanitizer.cpp
  MemorySanitizer.cpp
  XSanitizerCompositor.cpp
  ${XSAN_PASS_COMMON}
)

add_llvm_pass_plugin(TestPass
  PARTIAL_SOURCES_INTENDED # Skip checking for else files
  TestPass.cpp
  ${XSAN_PASS_COMMON}
)

target_compile_definitions(XSanInstPass PRIVATE XSAN_PASS)

set_target_properties(ASanInstPass
  TSanInstPass
  CovSanInstPass
  MSanInstPass
  XSanInstPass
  TestPass
  PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${XSAN_OUTPUT_PASSDIR}"
)

install(TARGETS ASanInstPass TSanInstPass CovSanInstPass MSanInstPass XSanInstPass
  LIBRARY
  DESTINATION ${XSAN_INSTALL_PASSDIR}
  COMPONENT pass
)

# add_llvm_component_library(LLVMInstrumentation
# AddressSanitizer.cpp
# MemorySanitizer.cpp
# SanitizerCoverage.cpp
# ThreadSanitizer.cpp
# HWAddressSanitizer.cpp

# ADDITIONAL_HEADER_DIRS
# ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms

# DEPENDS
# intrinsics_gen

# LINK_COMPONENTS
# Analysis
# Core
# Demangle
# MC
# Support
# TransformUtils
# ProfileData
# )
