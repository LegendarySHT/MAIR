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

# MAIR_USE_MOPIR_ASAN_* 选项在 ../CMakeLists.txt（src）中声明。

set(MOPIR_ASAN_ADAPTER_SRCS
  "${CMAKE_CURRENT_SOURCE_DIR}/MopIRImplementation/lib/MOP/AsanRedundancyAdapter.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/MopIRImplementation/lib/MOP/Passes/RedundantCheckEliminationPass.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/MopIRImplementation/lib/MOP/Analysis/MopRedundancyAnalysisPass.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/MopIRImplementation/lib/MOP/Analysis/ActiveMopAnalysisAdapter.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/MopIRImplementation/lib/MOP/Analysis/RecurringGraph.cpp"
)
set(MOPIR_ASAN_ADAPTER_INC "${CMAKE_CURRENT_SOURCE_DIR}/MopIRImplementation/include")

set(MOPIR_LOOP_RELOC_SRCS
  "${CMAKE_CURRENT_SOURCE_DIR}/MopIRImplementation/lib/MOP/LLVM/LoopInvariantRelocation.cpp"
)

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

set(MOPIR_ASAN_INSTPASS_EXTRA_SRCS)
if(MAIR_USE_MOPIR_ASAN_REDUNDANCY)
  list(APPEND MOPIR_ASAN_INSTPASS_EXTRA_SRCS ${MOPIR_ASAN_ADAPTER_SRCS})
endif()
if(MAIR_USE_MOPIR_ASAN_LOOP_RELOC)
  list(APPEND MOPIR_ASAN_INSTPASS_EXTRA_SRCS ${MOPIR_LOOP_RELOC_SRCS})
endif()

if(MOPIR_ASAN_INSTPASS_EXTRA_SRCS)
  target_sources(ASanInstPass PRIVATE ${MOPIR_ASAN_INSTPASS_EXTRA_SRCS})
  target_include_directories(ASanInstPass PRIVATE ${MOPIR_ASAN_ADAPTER_INC})
  target_compile_definitions(ASanInstPass PRIVATE MOP_IR_USE_LLVM)
endif()
if(MAIR_USE_MOPIR_ASAN_REDUNDANCY)
  target_compile_definitions(ASanInstPass PRIVATE MAIR_USE_MOPIR_ASAN_REDUNDANCY)
endif()
if(MAIR_USE_MOPIR_ASAN_LOOP_RELOC)
  target_compile_definitions(ASanInstPass PRIVATE MAIR_USE_MOPIR_ASAN_LOOP_RELOC)
endif()

if(MAIR_USE_MOPIR_ASAN_REDUNDANCY)
  target_sources(XSanInstPass PRIVATE ${MOPIR_ASAN_ADAPTER_SRCS})
  target_include_directories(XSanInstPass PRIVATE ${MOPIR_ASAN_ADAPTER_INC})
  target_compile_definitions(XSanInstPass PRIVATE
    MAIR_USE_MOPIR_ASAN_REDUNDANCY
    MOP_IR_USE_LLVM
  )
endif()
if(MAIR_USE_MOPIR_ASAN_LOOP_RELOC)
  target_sources(XSanInstPass PRIVATE ${MOPIR_LOOP_RELOC_SRCS})
  target_include_directories(XSanInstPass PRIVATE ${MOPIR_ASAN_ADAPTER_INC})
  target_compile_definitions(XSanInstPass PRIVATE
    MAIR_USE_MOPIR_ASAN_LOOP_RELOC
    MOP_IR_USE_LLVM
  )
endif()

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
