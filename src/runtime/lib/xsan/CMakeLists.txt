# Build for the XSan runtime support library.
set(ASAN_SOURCES
  # asan/orig/asan_allocator.cpp
  asan/orig/asan_activation.cpp
  asan/orig/asan_debugging.cpp
  asan/orig/asan_descriptions.cpp
  asan/orig/asan_errors.cpp
  asan/orig/asan_fake_stack.cpp
  asan/orig/asan_flags.cpp
  asan/orig/asan_fuchsia.cpp
  asan/orig/asan_globals.cpp
  asan/orig/asan_globals_win.cpp
  # asan/orig/asan_interceptors.cpp
  # asan/orig/asan_interceptors_memintrinsics.cpp
  # asan/orig/asan_malloc_linux.cpp
  # asan/orig/asan_malloc_mac.cpp
  asan/orig/asan_linux.cpp
  asan/orig/asan_mac.cpp
  asan/orig/asan_malloc_win.cpp
  asan/orig/asan_memory_profile.cpp
  asan/orig/asan_poisoning.cpp
  asan/orig/asan_posix.cpp
  asan/orig/asan_premap_shadow.cpp
  asan/orig/asan_report.cpp
  asan/orig/asan_rtl.cpp
  asan/orig/asan_shadow_setup.cpp
  asan/orig/asan_stack.cpp
  asan/orig/asan_stats.cpp
  asan/orig/asan_suppressions.cpp
  # asan/orig/asan_thread.cpp
  asan/orig/asan_win.cpp
)

set(XSAN_ASAN_SOURCES
  asan/asan_allocator.cpp
  asan/asan_thread.cpp
  asan/asan_init.cpp
  ${ASAN_SOURCES}
)

set(XSAN_SOURCES
  xsan_activation.cpp
  xsan_allocator.cpp
  xsan_disability_dummy.cpp
  xsan_flags.cpp
  xsan_interceptors.cpp
  xsan_interceptors_memintrinsics.cpp
  xsan_linux.cpp
  xsan_malloc_linux.cpp
  xsan_rtl.cpp
  xsan_thread.cpp
  ${XSAN_ASAN_SOURCES}
  )



if (NOT WIN32 AND NOT APPLE)
  list(APPEND XSAN_SOURCES
    # xsan_interceptors_vfork.S
    asan/orig/asan_interceptors_vfork.S
    )
endif()
set(XSAN_CXX_SOURCES
  xsan_new_delete.cpp
  )

set(XSAN_STATIC_SOURCES
  asan/orig/asan_rtl_static.cpp
  )

if (NOT WIN32 AND NOT APPLE)
  list(APPEND XSAN_STATIC_SOURCES
    asan/orig/asan_rtl_x86_64.S
  )
endif()

set(XSAN_PREINIT_SOURCES
  xsan_preinit.cpp
  )

set(ASAN_HEADERS
  asan/orig/asan_activation.h
  asan/orig/asan_activation_flags.inc
  asan/orig/asan_descriptions.h
  asan/orig/asan_errors.h
  asan/orig/asan_fake_stack.h
  asan/orig/asan_flags.h
  asan/orig/asan_flags.inc
  asan/orig/asan_init_version.h
  asan/orig/asan_interceptors.h
  asan/orig/asan_interceptors_memintrinsics.h
  asan/orig/asan_interface.inc
  asan/orig/asan_interface_internal.h
  asan/orig/asan_internal.h
  asan/orig/asan_lock.h
  asan/orig/asan_mapping.h
  asan/orig/asan_poisoning.h
  asan/orig/asan_premap_shadow.h
  asan/orig/asan_report.h
  asan/orig/asan_scariness_score.h
  asan/orig/asan_stack.h
  asan/orig/asan_stats.h
  asan/orig/asan_suppressions.h
  asan/orig/asan_thread.h
  )
set(XSAN_ASAN_HEADERS
  asan/asan_allocator.h
  asan/asan_mapping.h
  asan/asan_thread.h
  asan/asan_init.h
  ${ASAN_HEADERS}
)

SET(XSAN_HEADERS
  xsan_activation.h
  xsan_allocator.h
  xsan_flags.h
  xsan_interceptors.h
  xsan_interceptors_memintrinsics.h
  xsan_interface_internal.h
  xsan_internal.h
  xsan_thread.h
  ${XSAN_ASAN_HEADERS}
  )

# include_directories(..)
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/asan
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../
    ${CMAKE_CURRENT_SOURCE_DIR}/asan/orig
)



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
  add_compiler_rt_object_libraries(RTXsan_cxx
    ARCHS ${XSAN_SUPPORTED_ARCH}
    SOURCES ${XSAN_CXX_SOURCES}
    ADDITIONAL_HEADERS ${XSAN_HEADERS}
    CFLAGS ${XSAN_CFLAGS}
    DEFS ${XSAN_COMMON_DEFINITIONS})
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

  add_compiler_rt_runtime(clang_rt.xsan_cxx
      STATIC
      ARCHS ${ASAN_SUPPORTED_ARCH}
      OBJECT_LIBS RTXsan_cxx
                  RTUbsan_cxx
      CFLAGS ${ASAN_CFLAGS}
      DEFS ${ASAN_COMMON_DEFINITIONS}
      PARENT_TARGET asan)
  
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
      add_sanitizer_rt_symbols(clang_rt.xsan_cxx
        ARCHS ${arch})
      add_dependencies(xsan clang_rt.xsan_cxx-${arch}-symbols)
      add_sanitizer_rt_symbols(clang_rt.xsan
        ARCHS ${arch}
        EXTRA xsan.syms.extra)
      add_dependencies(xsan clang_rt.xsan-${arch}-symbols)
    endif()

  endforeach()
endif()