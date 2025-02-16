set(XSAN_LIT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(XSAN_TEST_DEPS ${XSAN_TEST_DEPS})
set(XSAN_TEST_ARCH ${XSAN_TEST_ARCH})
set(XSAN_TEST_TARGET_CC ${XSAN_TEST_TARGET_CC})



foreach(arch ${XSAN_TEST_ARCH})
  set(XSAN_TEST_TARGET_ARCH ${arch})
  set(XSAN_TEST_APPLE_PLATFORM "osx")
  set(XSAN_TEST_MIN_DEPLOYMENT_TARGET_FLAG "${DARWIN_osx_MIN_VER_FLAG}")
  string(TOLOWER "-${arch}-${OS_NAME}" XSAN_TEST_CONFIG_SUFFIX)
  get_bits_for_arch(${arch} XSAN_TEST_BITS)
  get_test_cc_for_arch(${arch} XSAN_TEST_TARGET_CC XSAN_TEST_TARGET_CFLAGS)
  if(ANDROID OR APPLE)
    set(XSAN_TEST_DYNAMIC True)
  else()
    set(XSAN_TEST_DYNAMIC False)
  endif()
  string(TOUPPER ${arch} ARCH_UPPER_CASE)
  set(CONFIG_NAME ${ARCH_UPPER_CASE}${OS_NAME}Config)
  if(NOT MINGW)
    # MinGW environments don't provide a statically linked CRT, so only the
    # dynamic xsan test configuration can be expected to work.
    configure_lit_site_cfg(
      ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
      ${CMAKE_CURRENT_BINARY_DIR}/${CONFIG_NAME}/lit.site.cfg.py
      )
    list(APPEND XSAN_TESTSUITES ${CMAKE_CURRENT_BINARY_DIR}/${CONFIG_NAME})
  endif()

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

### TODO: support unit test
# # Add unit tests.
# foreach(arch ${XSAN_TEST_ARCH})
#   string(TOUPPER ${arch} ARCH_UPPER_CASE)
#   set(CONFIG_NAME ${ARCH_UPPER_CASE}${OS_NAME}Config)
#   set(CONFIG_NAME_DYNAMIC ${ARCH_UPPER_CASE}${OS_NAME}DynamicConfig)

#   if(NOT MINGW)
#     # MinGW environments don't provide a statically linked CRT, so only the
#     # dynamic xsan test configuration can be expected to work.
#     set(XSAN_TEST_DYNAMIC False)
#     configure_lit_site_cfg(
#       ${CMAKE_CURRENT_SOURCE_DIR}/Unit/lit.site.cfg.py.in
#       ${CMAKE_CURRENT_BINARY_DIR}/Unit/${CONFIG_NAME}/lit.site.cfg.py)
#   endif()
#   if(COMPILER_RT_XSAN_HAS_STATIC_RUNTIME)
#     set(XSAN_TEST_DYNAMIC True)
#     configure_lit_site_cfg(
#       ${CMAKE_CURRENT_SOURCE_DIR}/Unit/lit.site.cfg.py.in
#       ${CMAKE_CURRENT_BINARY_DIR}/Unit/${CONFIG_NAME_DYNAMIC}/lit.site.cfg.py)
#   endif()
#   # FIXME: support unit test in the android test runner
#   if (NOT ANDROID)
#     if (NOT MINGW)
#       list(APPEND XSAN_TEST_DEPS AsanUnitTests)
#       list(APPEND XSAN_TESTSUITES ${CMAKE_CURRENT_BINARY_DIR}/Unit/${CONFIG_NAME})
#     endif()
#     if(COMPILER_RT_XSAN_HAS_STATIC_RUNTIME)
#       list(APPEND XSAN_DYNAMIC_TEST_DEPS AsanDynamicUnitTests)
#       list(APPEND XSAN_DYNAMIC_TESTSUITES ${CMAKE_CURRENT_BINARY_DIR}/Unit/${CONFIG_NAME_DYNAMIC})
#     endif()
#   endif()
# endforeach()

if (SHADOW_MAPPING_UNRELIABLE)
  set(exclude_from_check_all.g "EXCLUDE_FROM_CHECK_ALL")
else()
  set(exclude_from_check_all.g "")
endif()

add_lit_testsuite(check-xsan "Running the XSan-specific tests"
  ${XSAN_TESTSUITES}
  ${exclude_from_check_all}
  DEPENDS ${XSAN_TEST_DEPS})

### TODO: fit dynamic runtime
# if(COMPILER_RT_XSAN_HAS_STATIC_RUNTIME)
#   add_lit_testsuite(check-xsan-dynamic
#                     "Running the XSan tests with dynamic runtime"
#                     ${XSAN_DYNAMIC_TESTSUITES}
#                     ${exclude_from_check_all.g}
#                     DEPENDS ${XSAN_DYNAMIC_TEST_DEPS})
# endif()

list(APPEND XSAN_LIT_TEST_TARGETS check-xsan)
set(XSAN_LIT_TEST_TARGETS "${XSAN_LIT_TEST_TARGETS}" PARENT_SCOPE)

message_green("ready to test ASan/ASan@XSan")