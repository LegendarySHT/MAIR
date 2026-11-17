include(AddLLVM)

add_custom_target(MAIRUnitTests)
set_target_properties(MAIRUnitTests PROPERTIES FOLDER "Compiler-RT/Tests")

set(LLVM_LINK_COMPONENTS
  Analysis
  AsmParser
  Core
  TransformUtils
  Support
)

set(MAIR_GTEST_DIR ${LLVM_MAIN_SRC_DIR}/../third-party/unittest/googletest)

set(MAIR_TEST_SOURCES
  MAIRGraphTest.cpp
  MAIRLoopHierarchyTest.cpp
  MAIRMaterializationTest.cpp
  CommonObservationMotionPassTest.cpp
  LocationMergePassTest.cpp
  LoopInvariantObservationMotionPassTest.cpp
  LoopPeriodicObservationCombinePassTest.cpp
  RedundantObservationEliminationPassTest.cpp
  SimplifyGraphPassTest.cpp
)

if(EXISTS ${MAIR_GTEST_DIR}/src/gtest-all.cc)
  add_llvm_executable(MAIRGraphTest
    ${MAIR_TEST_SOURCES}
    ${MAIR_GTEST_DIR}/src/gtest-all.cc
    ${MAIR_GTEST_DIR}/src/gtest_main.cc
    $<TARGET_OBJECTS:MAIRCore>
  )
  target_link_libraries(MAIRGraphTest PRIVATE ${LLVM_PTHREAD_LIB})
else()
  # No LLVM source tree next to the installed LLVM -- which is the case for a
  # CI build against /usr/lib/llvm-N, where ${LLVM_MAIN_SRC_DIR} points at a
  # directory that does not exist. The gtest sources used to come from
  # llvm-project/third-party, so this fell through to add_unittest and died on
  # 'gtest/gtest.h' file not found. Prefer a system GTest instead.
  #
  # CONFIG mode on purpose: Debian/Ubuntu ship GTestConfig.cmake, which sets
  # GTest_FOUND and creates GTest::gtest / GTest::gtest_main. A bare
  # find_package tries CMake's FindGTest module first, which only sets
  # GTEST_FOUND (and GTest::GTest / GTest::Main), so the check below would miss
  # a perfectly usable install and fall through to add_unittest.
  find_package(GTest CONFIG QUIET)
  if(GTest_FOUND AND TARGET GTest::gtest_main)
    message_green("MAIR unit tests: using the system GTest")
    add_llvm_executable(MAIRGraphTest
      ${MAIR_TEST_SOURCES}
      $<TARGET_OBJECTS:MAIRCore>
    )
    target_link_libraries(MAIRGraphTest PRIVATE GTest::gtest GTest::gtest_main)
  else()
    add_unittest(MAIRUnitTests MAIRGraphTest
      ${MAIR_TEST_SOURCES}
      $<TARGET_OBJECTS:MAIRCore>
    )
  endif()
endif()

add_dependencies(MAIRUnitTests MAIRGraphTest)

target_include_directories(MAIRGraphTest BEFORE PRIVATE
  ${CMAKE_SOURCE_DIR}/src/instrumentation
  ${LLVM_BINARY_DIR}/include
  ${LLVM_MAIN_INCLUDE_DIR}
  ${MAIR_GTEST_DIR}/include
  ${MAIR_GTEST_DIR}
)

add_custom_target(check-mair-unit
  COMMAND $<TARGET_FILE:MAIRGraphTest> --gtest_brief=1
  DEPENDS MAIRGraphTest
  USES_TERMINAL
)
set_target_properties(check-mair-unit PROPERTIES FOLDER "Compiler-RT/Tests")
