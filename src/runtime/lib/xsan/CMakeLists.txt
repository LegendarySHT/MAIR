# Build for the XSan runtime support library.
set(XSAN_SOURCES
  xsan_activation.cpp
  xsan_rtl.cpp
  )

if (NOT WIN32 AND NOT APPLE)
  list(APPEND XSAN_SOURCES
    # xsan_interceptors_vfork.S
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

SET(XSAN_HEADERS
  xsan_activation.h
  xsan_interface_internal.h
  xsan_internal.h
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
endif()