# This file is modified from src/runtime/test/asan/CMakeLists.txt
set(XSAN_TEST_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SANITIZER_COMMON_LIT_TEST_DEPS)
# When ANDROID, we build tests with the host compiler (i.e. CMAKE_C_COMPILER),
# and run tests with tools from the host toolchain.
if(NOT ANDROID)
  # Tests use C++ standard library headers.
  if (TARGET cxx-headers OR HAVE_LIBCXX)
    list(APPEND SANITIZER_COMMON_LIT_TEST_DEPS cxx-headers)
  endif()
endif()

set(XSAN_LIT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(XSAN_TESTSUITES)
set(XSAN_DYNAMIC_TESTSUITES)

# Before Windows 8 (CMAKE_SYSTEM_VERSION 6.2), reserving large regions of shadow
# memory allocated physical memory for page tables, which made it very
# unreliable. Remove the XSan tests from check-all in this configuration.
set(SHADOW_MAPPING_UNRELIABLE FALSE)
if(OS_NAME MATCHES "Windows" AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND
    CMAKE_SYSTEM_VERSION LESS 6.2)
  set(SHADOW_MAPPING_UNRELIABLE TRUE)
  message(WARNING "Disabling XSan tests because they are unreliable on Windows 7 and earlier")
endif()

macro(get_bits_for_arch arch bits)
  if (${arch} MATCHES "x86_64|powerpc64|powerpc64le|aarch64|arm64|mips64|mips64el|s390x|sparcv9|riscv64")
    set(${bits} 64)
  elseif (${arch} MATCHES "i386|arm|mips|mipsel|sparc")
    set(${bits} 32)
  else()
    message(FATAL_ERROR "Unknown target architecture: ${arch}")
  endif()
endmacro()

set(XSAN_TEST_DEPS ${SANITIZER_COMMON_LIT_TEST_DEPS})
list(APPEND XSAN_TEST_DEPS asan tsan ubsan xclang)
set(XSAN_DYNAMIC_TEST_DEPS ${XSAN_TEST_DEPS})

## TODO: support different architectures
set(XSAN_TEST_ARCH ${XSAN_SUPPORTED_ARCH})
if(APPLE)
  darwin_filter_host_archs(XSAN_SUPPORTED_ARCH XSAN_TEST_ARCH)
endif()
list(REMOVE_ITEM XSAN_TEST_ARCH sparc sparcv9)
if(OS_NAME MATCHES "SunOS")
  list(REMOVE_ITEM XSAN_TEST_ARCH x86_64)
endif()

set(XCLANG_PATH "${XSAN_BIN_DIR}/xclang")
set(XSAN_TEST_TARGET_CC ${XCLANG_PATH})

foreach(arch ${XSAN_TEST_ARCH})
  set(XSAN_TEST_TARGET_ARCH ${arch})
  set(XSAN_TEST_APPLE_PLATFORM "osx")
  set(XSAN_TEST_MIN_DEPLOYMENT_TARGET_FLAG "${DARWIN_osx_MIN_VER_FLAG}")
  string(TOLOWER "-${arch}-${OS_NAME}" XSAN_TEST_CONFIG_SUFFIX)
  get_bits_for_arch(${arch} XSAN_TEST_BITS)
  # For cross-compiling, we need to set the target compiler and flags.
  # TODO: support different architectures
  # get_test_cc_for_arch(${arch} XSAN_TEST_TARGET_CC XSAN_TEST_TARGET_CFLAGS)
  if(ANDROID OR APPLE)
    set(XSAN_TEST_DYNAMIC True)
  else()
    set(XSAN_TEST_DYNAMIC False)
  endif()
  string(TOUPPER ${arch} ARCH_UPPER_CASE)
  set(CONFIG_NAME ${ARCH_UPPER_CASE}${OS_NAME}Config)
  configure_lit_site_cfg(
    ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
    ${CMAKE_CURRENT_BINARY_DIR}/${CONFIG_NAME}/lit.site.cfg.py
    )
  list(APPEND XSAN_TESTSUITES ${CMAKE_CURRENT_BINARY_DIR}/${CONFIG_NAME})

  if(COMPILER_RT_XSAN_HAS_STATIC_RUNTIME)
    string(TOLOWER "-${arch}-${OS_NAME}-dynamic" XSAN_TEST_CONFIG_SUFFIX)
    set(XSAN_TEST_DYNAMIC True)
    set(CONFIG_NAME ${ARCH_UPPER_CASE}${OS_NAME}DynamicConfig)
    configure_lit_site_cfg(
      ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
      ${CMAKE_CURRENT_BINARY_DIR}/${CONFIG_NAME}/lit.site.cfg.py)
    list(APPEND XSAN_DYNAMIC_TESTSUITES
      ${CMAKE_CURRENT_BINARY_DIR}/${CONFIG_NAME})
  endif()

  # ARM on Linux may include thumb code, where fast unwinding does not work.
  # Enable unwind tables so that we do not end up falling back to it in tests.
  if ("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux" AND NOT ANDROID AND ${arch} MATCHES "arm")
    set(XSAN_TEST_TARGET_CFLAGS "${XSAN_TEST_TARGET_CFLAGS} -funwind-tables")
  endif()
endforeach()

# iOS and iOS simulator test suites
# These are not added into "check-all", in order to run these tests, use
# "check-xsan-iossim-x86_64" and similar. They also require that an extra env
# variable to select which iOS device or simulator to use, e.g.:
# SANITIZER_IOSSIM_TEST_DEVICE_IDENTIFIER="iPhone 6"
if(APPLE)
  set(XSAN_TEST_TARGET_CC ${COMPILER_RT_TEST_COMPILER})
  set(XSAN_TEST_DYNAMIC True)
  set(XSAN_APPLE_PLATFORMS ${SANITIZER_COMMON_SUPPORTED_OS})

  foreach(platform ${XSAN_APPLE_PLATFORMS})
    if ("${platform}" STREQUAL "osx")
      # Skip macOS because it's handled by the code above that builds tests for the host machine.
      continue()
    endif()
    list_intersect(
      XSAN_TEST_${platform}_ARCHS
      XSAN_SUPPORTED_ARCH
      DARWIN_${platform}_ARCHS
    )
    foreach(arch ${XSAN_TEST_${platform}_ARCHS})
      get_test_cflags_for_apple_platform(
        "${platform}"
        "${arch}"
        XSAN_TEST_TARGET_CFLAGS
        )
      string(TOUPPER "${arch}" ARCH_UPPER_CASE)
      get_capitalized_apple_platform("${platform}" PLATFORM_CAPITALIZED)
      set(CONFIG_NAME "${PLATFORM_CAPITALIZED}${ARCH_UPPER_CASE}Config")
      set(XSAN_TEST_CONFIG_SUFFIX "-${arch}-${platform}")
      set(XSAN_TEST_APPLE_PLATFORM "${platform}")
      set(XSAN_TEST_TARGET_ARCH "${arch}")
      set(XSAN_TEST_MIN_DEPLOYMENT_TARGET_FLAG "${DARWIN_${platform}_MIN_VER_FLAG}")
      get_bits_for_arch(${arch} XSAN_TEST_BITS)
      configure_lit_site_cfg(
        ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
        ${CMAKE_CURRENT_BINARY_DIR}/${CONFIG_NAME}/lit.site.cfg.py
        )
      add_lit_testsuite(check-xsan-${platform}-${arch} "XSan ${platform} ${arch} tests"
        ${CMAKE_CURRENT_BINARY_DIR}/${CONFIG_NAME}/
        EXCLUDE_FROM_CHECK_ALL
        DEPENDS ${XSAN_TEST_DEPS})
    endforeach()
  endforeach()
endif()

if (SHADOW_MAPPING_UNRELIABLE)
  set(exclude_from_check_all.g "EXCLUDE_FROM_CHECK_ALL")
else()
  set(exclude_from_check_all.g "")
endif()

add_lit_testsuite(check-xsan "Running the XSan tests"
  ${XSAN_TESTSUITES}
  ${exclude_from_check_all}
  DEPENDS ${XSAN_TEST_DEPS})
set_target_properties(check-xsan PROPERTIES FOLDER "Compiler-RT Misc")

# TODO: fit dynamic runtime
# if(COMPILER_RT_XSAN_HAS_STATIC_RUNTIME)
#   add_lit_testsuite(check-xsan-dynamic
#                     "Running the XSan tests with dynamic runtime"
#                     ${XSAN_DYNAMIC_TESTSUITES}
#                     ${exclude_from_check_all.g}
#                     DEPENDS ${XSAN_DYNAMIC_TEST_DEPS})
#   set_target_properties(check-xsan-dynamic
#                         PROPERTIES FOLDER "Compiler-RT Misc")
# endif()

# if (QSYM_BACKEND)
#   set(SYM_TEST_FILECHECK_ARGS "--check-prefix=QSYM --check-prefix=ANY")
# else()
#   set(SYM_TEST_FILECHECK_ARGS "--check-prefix=SIMPLE --check-prefix=ANY")
# endif()

# if (${LLVM_VERSION_MAJOR} VERSION_GREATER_EQUAL 14)
#   # FileCheck used to be fine with unused prefixes when more than one prefix was
#   # defined. This changed in LLVM version 14, requiring the new option
#   # "--allow-unused-prefixes" (added in LLVM 13) to restore the old behavior.
#   set(SYM_TEST_FILECHECK_ARGS "${SYM_TEST_FILECHECK_ARGS} --allow-unused-prefixes")
# endif()

# configure_file("lit.site.cfg.in" "lit.site.cfg")

# add_custom_target(check
#   lit
#   --verbose
#   --path=${LLVM_TOOLS_BINARY_DIR}
#   ${CMAKE_CURRENT_BINARY_DIR}
#   COMMENT "Testing the system..."
#   USES_TERMINAL)

# add_dependencies(check SymRuntime Symbolize)
# if (TARGET SymRuntime32)
#   add_dependencies(check SymRuntime32)
# endif()
