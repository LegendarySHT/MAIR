# Build for the XSan runtime support library.
set(XSAN_ASAN_SOURCES
  ../asan/asan_allocator.cpp
  ../asan/asan_activation.cpp
  ../asan/asan_debugging.cpp
  ../asan/asan_descriptions.cpp
  ../asan/asan_errors.cpp
  ../asan/asan_fake_stack.cpp
  ../asan/asan_flags.cpp
  ../asan/asan_fuchsia.cpp
  ../asan/asan_globals.cpp
  ../asan/asan_globals_win.cpp
  # ../asan/asan_interceptors.cpp
  # ../asan/asan_interceptors_memintrinsics.cpp
  # ../asan/asan_malloc_linux.cpp
  # ../asan/asan_malloc_mac.cpp
  ../asan/asan_linux.cpp
  ../asan/asan_mac.cpp
  ../asan/asan_malloc_win.cpp
  ../asan/asan_memory_profile.cpp
  ../asan/asan_poisoning.cpp
  ../asan/asan_posix.cpp
  ../asan/asan_premap_shadow.cpp
  ../asan/asan_report.cpp
  ../asan/asan_rtl.cpp
  ../asan/asan_shadow_setup.cpp
  ../asan/asan_stack.cpp
  ../asan/asan_stats.cpp
  ../asan/asan_suppressions.cpp
  ../asan/asan_thread.cpp
  ../asan/asan_win.cpp
)
set(XSAN_SOURCES
  xsan_activation.cpp
  xsan_disability_dummy.cpp
  xsan_flags.cpp
  xsan_interceptors.cpp
  xsan_interceptors_memintrinsics.cpp
  xsan_linux.cpp
  xsan_malloc_linux.cpp
  xsan_rtl.cpp
  ${XSAN_ASAN_SOURCES}
  )



if (NOT WIN32 AND NOT APPLE)
  list(APPEND XSAN_SOURCES
    # xsan_interceptors_vfork.S
    ../asan/asan_interceptors_vfork.S
    )
endif()
set(XSAN_CXX_SOURCES
  # xsan_new_delete.cpp
  )

set(XSAN_STATIC_SOURCES
  ../asan/asan_rtl_static.cpp
  )

if (NOT WIN32 AND NOT APPLE)
  list(APPEND XSAN_STATIC_SOURCES
    ../asan/asan_rtl_x86_64.S
  )
endif()

set(XSAN_PREINIT_SOURCES
  xsan_preinit.cpp
  )

set(XSAN_ASAN_HEADERS
  ../asan/asan_activation.h
  ../asan/asan_activation_flags.inc
  ../asan/asan_allocator.h
  ../asan/asan_descriptions.h
  ../asan/asan_errors.h
  ../asan/asan_fake_stack.h
  ../asan/asan_flags.h
  ../asan/asan_flags.inc
  ../asan/asan_init_version.h
  ../asan/asan_interceptors.h
  ../asan/asan_interceptors_memintrinsics.h
  ../asan/asan_interface.inc
  ../asan/asan_interface_internal.h
  ../asan/asan_internal.h
  ../asan/asan_lock.h
  ../asan/asan_mapping.h
  ../asan/asan_poisoning.h
  ../asan/asan_premap_shadow.h
  ../asan/asan_report.h
  ../asan/asan_scariness_score.h
  ../asan/asan_stack.h
  ../asan/asan_stats.h
  ../asan/asan_suppressions.h
  ../asan/asan_thread.h
)

SET(XSAN_HEADERS
  xsan_activation.h
  xsan_flags.h
  xsan_interceptors.h
  xsan_interceptors_memintrinsics.h
  xsan_interface_internal.h
  xsan_internal.h
  ${XSAN_ASAN_HEADERS}
  )

include_directories(..)


set(XSAN_COMMON_RUNTIME_OBJECT_LIBS
  RTInterception
  RTSanitizerCommon
  RTSanitizerCommonLibc
  RTSanitizerCommonCoverage
  RTSanitizerCommonSymbolizer
  RTLSanCommon
  RTUbsan)


set(XSAN_CFLAGS ${SANITIZER_COMMON_CFLAGS})
set(XSAN_COMMON_DEFINITIONS)

append_rtti_flag(OFF XSAN_CFLAGS)

# Silence warnings in system headers with MSVC.
if(NOT CLANG_CL)
  append_list_if(COMPILER_RT_HAS_EXTERNAL_FLAG "/experimental:external /external:W0 /external:anglebrackets" XSAN_CFLAGS)
endif()

# Too many existing bugs, needs cleanup.
append_list_if(COMPILER_RT_HAS_WNO_FORMAT -Wno-format XSAN_CFLAGS)

if(NOT APPLE)
  add_compiler_rt_object_libraries(RTXsan
    ARCHS ${XSAN_SUPPORTED_ARCH}
    SOURCES ${XSAN_SOURCES}
    ADDITIONAL_HEADERS ${XSAN_HEADERS}
    CFLAGS ${XSAN_CFLAGS}
    DEFS ${XSAN_COMMON_DEFINITIONS})
  # add_compiler_rt_object_libraries(RTXsan_cxx
  #   ARCHS ${XSAN_SUPPORTED_ARCH}
  #   SOURCES ${XSAN_CXX_SOURCES}
  #   ADDITIONAL_HEADERS ${XSAN_HEADERS}
  #   CFLAGS ${XSAN_CFLAGS}
  #   DEFS ${XSAN_COMMON_DEFINITIONS})
  add_compiler_rt_object_libraries(RTXsan_static
    ARCHS ${XSAN_SUPPORTED_ARCH}
    SOURCES ${XSAN_STATIC_SOURCES}
    ADDITIONAL_HEADERS ${XSAN_HEADERS}
    CFLAGS ${XSAN_CFLAGS}
    DEFS ${XSAN_COMMON_DEFINITIONS})
  add_compiler_rt_object_libraries(RTXsan_preinit
    ARCHS ${XSAN_SUPPORTED_ARCH}
    SOURCES ${XSAN_PREINIT_SOURCES}
    ADDITIONAL_HEADERS ${XSAN_HEADERS}
    CFLAGS ${XSAN_CFLAGS}
    DEFS ${XSAN_COMMON_DEFINITIONS})

  # file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp "")
  # add_compiler_rt_object_libraries(RTXsan_dynamic_version_script_dummy
  #   ARCHS ${XSAN_SUPPORTED_ARCH}
  #   SOURCES ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp
  #   CFLAGS ${XSAN_DYNAMIC_CFLAGS}
  #   DEFS ${XSAN_DYNAMIC_DEFINITIONS})
endif()

add_compiler_rt_component(xsan)

if(NOT APPLE)

  add_compiler_rt_runtime(clang_rt.xsan
      STATIC
      ARCHS ${XSAN_SUPPORTED_ARCH}
      OBJECT_LIBS RTXsan_preinit
                  RTXsan          
                  ${XSAN_COMMON_RUNTIME_OBJECT_LIBS}
      CFLAGS ${XSAN_CFLAGS}
      DEFS ${XSAN_COMMON_DEFINITIONS}
      PARENT_TARGET xsan)

  add_compiler_rt_runtime(clang_rt.xsan_static
      STATIC
      ARCHS ${XSAN_SUPPORTED_ARCH}
      OBJECT_LIBS RTXsan_static
      CFLAGS ${XSAN_CFLAGS}
      DEFS ${XSAN_COMMON_DEFINITIONS}
      PARENT_TARGET xsan)
  
  add_compiler_rt_runtime(clang_rt.xsan-preinit
      STATIC
      ARCHS ${XSAN_SUPPORTED_ARCH}
      OBJECT_LIBS RTXsan_preinit
      CFLAGS ${XSAN_CFLAGS}
      DEFS ${XSAN_COMMON_DEFINITIONS}
      PARENT_TARGET xsan)

  foreach(arch ${XSAN_SUPPORTED_ARCH})
    if (SANITIZER_USE_SYMBOLS AND NOT ${arch} STREQUAL "i386")
      # add_sanitizer_rt_symbols(clang_rt.xsan_cxx
      #   ARCHS ${arch})
      # add_dependencies(xsan clang_rt.xsan_cxx-${arch}-symbols)
      add_sanitizer_rt_symbols(clang_rt.xsan
        ARCHS ${arch}
        EXTRA xsan.syms.extra)
      add_dependencies(xsan clang_rt.xsan-${arch}-symbols)
    endif()

  endforeach()
endif()