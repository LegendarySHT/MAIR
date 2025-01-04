########## Set the sources for the XSan runtime library ##########

# Build for the XSan runtime support library.
set(ASAN_SOURCES
  # asan/orig/asan_allocator.cpp
  asan/orig/asan_activation.cpp
  asan/orig/asan_debugging.cpp
  asan/orig/asan_descriptions.cpp
  asan/orig/asan_errors.cpp
  asan/orig/asan_fake_stack.cpp
  # asan/orig/asan_flags.cpp
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
  # asan/orig/asan_poisoning.cpp
  asan/orig/asan_posix.cpp
  asan/orig/asan_premap_shadow.cpp
  #asan/orig/asan_report.cpp
  # asan/orig/asan_rtl.cpp
  asan/orig/asan_shadow_setup.cpp
  asan/orig/asan_stack.cpp
  asan/orig/asan_stats.cpp
  asan/orig/asan_suppressions.cpp
  # asan/orig/asan_thread.cpp
  asan/orig/asan_win.cpp
)

if(WIN32)
  set(ASAN_DYNAMIC_RUNTIME_THUNK_SOURCES
    asan_globals_win.cpp
    asan_win_common_runtime_thunk.cpp
    asan_win_dynamic_runtime_thunk.cpp
    )
  set(ASAN_STATIC_RUNTIME_THUNK_SOURCES
    asan_globals_win.cpp
    asan_malloc_win_thunk.cpp
    asan_win_common_runtime_thunk.cpp
    asan_win_static_runtime_thunk.cpp
    )
endif()

set(XSAN_ASAN_SOURCES
  asan/asan_allocator.cpp
  asan/asan_flags.cpp
  asan/asan_poisoning.cpp
  asan/asan_report.cpp
  asan/asan_rtl.cpp
  asan/asan_rtl_extra.cpp
  asan/asan_thread.cpp
  ${ASAN_SOURCES}
)



set(TSAN_SOURCES
  tsan/orig/tsan_debugging.cpp
  tsan/orig/tsan_external.cpp
  # tsan/orig/tsan_fd.cpp
  # tsan/orig/tsan_flags.cpp
  tsan/orig/tsan_ignoreset.cpp
  # tsan/orig/tsan_interceptors_posix.cpp
  tsan/orig/tsan_interface.cpp
  tsan/orig/tsan_interface_ann.cpp
  tsan/orig/tsan_interface_atomic.cpp
  tsan/orig/tsan_interface_java.cpp
  tsan/orig/tsan_malloc_mac.cpp
  tsan/orig/tsan_md5.cpp
  # tsan/orig/tsan_mman.cpp
  tsan/orig/tsan_mutexset.cpp
  tsan/orig/tsan_report.cpp
  tsan/orig/tsan_rtl.cpp
  tsan/orig/tsan_rtl_access.cpp
  tsan/orig/tsan_rtl_mutex.cpp
  tsan/orig/tsan_rtl_proc.cpp
  # tsan/orig/tsan_rtl_report.cpp
  # tsan/orig/tsan_rtl_thread.cpp
  # tsan/orig/tsan_stack_trace.cpp
  tsan/orig/tsan_suppressions.cpp
  tsan/orig/tsan_symbolize.cpp
  #tsan/orig/tsan_sync.cpp
  tsan/orig/tsan_vector_clock.cpp
)

if(APPLE)
  list(APPEND TSAN_SOURCES
    # tsan/orig/tsan_interceptors_mac.cpp
    # tsan/orig/tsan_interceptors_mach_vm.cpp
    tsan/orig/tsan_platform_mac.cpp
    # tsan/orig/tsan_platform_posix.cpp
    tsan/tsan_platform_posix.cpp
    )
    elseif(UNIX)
    # Assume Linux
    list(APPEND TSAN_SOURCES
    # tsan/orig/tsan_platform_linux.cpp
    # tsan/orig/tsan_platform_posix.cpp
    tsan/tsan_platform_linux.cpp
    tsan/tsan_platform_posix.cpp
    )
endif()

if(COMPILER_RT_INTERCEPT_LIBDISPATCH)
  list(APPEND TSAN_SOURCES
    # TODO: provide uniform interceptors for libdispatch
    tsan/orig/tsan_interceptors_libdispatch.cpp
    )
endif()


set(TSAN_ASM_SOURCES)

if (NOT APPLE)
  foreach(arch ${TSAN_SUPPORTED_ARCH})
    if(arch STREQUAL "x86_64")
      set(TSAN_ASM_SOURCES
        tsan/orig/tsan_rtl_amd64.S
        )
      # Go is not supported.
    elseif(arch STREQUAL "aarch64")
      set(TSAN_ASM_SOURCES
        tsan/orig/tsan_rtl_aarch64.S
        )
      # Go is not supported.
    elseif(arch MATCHES "loongarch64")
      set(TSAN_ASM_SOURCES
        tsan/orig/tsan_rtl_loongarch64.S
        )
    elseif(arch MATCHES "powerpc64|powerpc64le")
      set(TSAN_ASM_SOURCES
        tsan/orig/tsan_rtl_ppc64.S
        )
    elseif(arch MATCHES "mips64|mips64le")
      set(TSAN_ASM_SOURCES
        tsan/orig/tsan_rtl_mips64.S
        )
    elseif(arch MATCHES "riscv64")
      set(TSAN_ASM_SOURCES
        tsan/orig/tsan_rtl_riscv64.S
        )
    elseif(arch MATCHES "s390x")
      set(TSAN_ASM_SOURCES
        tsan/orig/tsan_rtl_s390x.S
        )
    else()
    endif()
  endforeach()
endif()

set(XSAN_TSAN_SOURCES
  tsan/tsan_fd.cpp
  tsan/tsan_flags.cpp
  tsan/tsan_init.cpp
  tsan/tsan_interceptors.cpp
  tsan/tsan_mman.cpp
  tsan/tsan_rtl_extra.cpp
  tsan/tsan_rtl_report.cpp
  tsan/tsan_rtl_thread.cpp
  tsan/tsan_stack_trace.cpp
  tsan/tsan_sync.cpp
  ${TSAN_SOURCES}
  ${TSAN_ASM_SOURCES}
)


set(XSAN_SOURCES
  xsan_activation.cpp
  xsan_allocator.cpp
  xsan_disability_dummy.cpp
  xsan_flags.cpp
  xsan_hooks.cpp
  xsan_interceptors.cpp
  xsan_interceptors_memintrinsics.cpp
  xsan_interface.cpp
  xsan_linux.cpp
  xsan_malloc_linux.cpp
  xsan_posix.cpp
  xsan_rtl.cpp
  xsan_report.cpp
  xsan_thread.cpp
  ${XSAN_ASAN_SOURCES}
  ${XSAN_TSAN_SOURCES}
  )



if (NOT WIN32 AND NOT APPLE)
  list(APPEND XSAN_SOURCES
    xsan_interceptors_vfork.S # FIXME: TSan uses fork to replace vfork. Should we do the same?
    # asan/orig/asan_interceptors_vfork.S
    )
endif()


set(XSAN_CXX_SOURCES
  xsan_new_delete.cpp
  )

set(XSAN_STATIC_SOURCES
  asan/orig/asan_rtl_static.cpp
  xsan_rtl_static.cpp
  )

if ("x86_64" IN_LIST ASAN_SUPPORTED_ARCH AND NOT WIN32 AND NOT APPLE)
  list(APPEND XSAN_STATIC_SOURCES
    asan/orig/asan_rtl_x86_64.S
  )
endif()

set(XSAN_PREINIT_SOURCES
  xsan_preinit.cpp
  )

################## Set the headers for the XSan runtime library ##########

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
  # asan/orig/asan_mapping.h
  asan/orig/asan_poisoning.h
  asan/orig/asan_premap_shadow.h
  asan/orig/asan_report.h
  asan/orig/asan_scariness_score.h
  asan/orig/asan_stack.h
  asan/orig/asan_stats.h
  asan/orig/asan_suppressions.h
  # asan/orig/asan_thread.h
  )
set(XSAN_ASAN_HEADERS
  asan/asan_allocator.h
  asan/asan_mapping.h
  asan/asan_thread.h
  asan/asan_init.h
  ${ASAN_HEADERS}
)


set(TSAN_HEADERS
  tsan/orig/tsan_defs.h
  tsan/orig/tsan_dense_alloc.h
  tsan/orig/tsan_fd.h
  tsan/orig/tsan_flags.h
  tsan/orig/tsan_flags.inc
  tsan/orig/tsan_ignoreset.h
  tsan/orig/tsan_ilist.h
  # tsan/orig/tsan_interceptors.h
  tsan/orig/tsan_interface.h
  # tsan/orig/tsan_interface.inc
  tsan/orig/tsan_interface_ann.h
  tsan/orig/tsan_interface_java.h
  tsan/orig/tsan_mman.h
  tsan/orig/tsan_mutexset.h
  # tsan/orig/tsan_platform.h
  tsan/orig/tsan_ppc_regs.h
  tsan/orig/tsan_report.h
  # tsan/orig/tsan_rtl.h
  tsan/orig/tsan_shadow.h
  tsan/orig/tsan_stack_trace.h
  tsan/orig/tsan_suppressions.h
  tsan/orig/tsan_symbolize.h
  tsan/orig/tsan_sync.h
  tsan/orig/tsan_trace.h
  tsan/orig/tsan_vector_clock.h
  )

set(XSAN_TSAN_HEADERS
  tsan/tsan_init.h
  tsan/tsan_interceptors.h
  tsan/tsan_interface.inc
  tsan/tsan_platform.h
  tsan/tsan_rtl.h
  tsan/tsan_rtl_extra.h
  ${TSAN_HEADERS}
)

SET(XSAN_HEADERS
  xsan_activation.h
  xsan_allocator.h
  xsan_flags.h
  xsan_hooks.h
  xsan_interceptors.h
  xsan_interceptors_memintrinsics.h
  xsan_interface_internal.h
  xsan_internal.h
  xsan_report.h
  xsan_thread.h
  ${XSAN_ASAN_HEADERS}
  ${XSAN_TSAN_HEADERS}
  )

# include_directories(..)
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/asan
    ${CMAKE_CURRENT_SOURCE_DIR}/tsan
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../
    ${CMAKE_CURRENT_SOURCE_DIR}/asan/orig
    ${CMAKE_CURRENT_SOURCE_DIR}/tsan/orig
)


# Set the 




############### Set the CFLAGS for the XSan runtime library ##########

set(ASAN_CFLAGS)

append_list_if(MSVC /Zl ASAN_CFLAGS)

set(ASAN_COMMON_DEFINITIONS "")

set(TSAN_CFLAGS)
# SANITIZER_COMMON_CFLAGS contains -fPIC, but it's performance-critical for
# TSan runtime to be built with -fPIE to reduce the number of register spills.
# On FreeBSD however it provokes linkage issue thus we disable it.
if(NOT CMAKE_SYSTEM MATCHES "FreeBSD")
  # -fPIE reduces the number of register spills in TSan runtime.
  # This is performance-critical.
  append_list_if(COMPILER_RT_HAS_FPIE_FLAG -fPIE TSAN_CFLAGS)
endif()

if(COMPILER_RT_TSAN_DEBUG_OUTPUT)
  # Add extra debug information to TSan runtime. This configuration is rarely
  # used, but we need to support it so that debug output will not bitrot.
  list(APPEND TSAN_CFLAGS -DTSAN_DEBUG_OUTPUT=2)
endif()
set(TSAN_RTL_CFLAGS ${TSAN_CFLAGS})
# -mssse3 enables SIMD instructions that are used in TSan runtime.
append_list_if(COMPILER_RT_HAS_MSSE4_2_FLAG -msse4.2 TSAN_RTL_CFLAGS)
# # compile-time: -Wframe-larger-than=530: limit frame size to 530. Maybe useless?
# append_list_if(SANITIZER_LIMIT_FRAME_SIZE -Wframe-larger-than=530
#                 TSAN_RTL_CFLAGS)
# # compile-time: -Wglobal-constructors: warn about global constructors. Maybe useless?
# append_list_if(COMPILER_RT_HAS_WGLOBAL_CONSTRUCTORS_FLAG -Wglobal-constructors
#                 TSAN_RTL_CFLAGS)

if(COMPILER_RT_INTERCEPT_LIBDISPATCH)
  # Intercept libdispatch functions in Apple platforms (Grand Central Dispatch).
  list(APPEND TSAN_RTL_CFLAGS ${COMPILER_RT_LIBDISPATCH_CFLAGS})
endif()

set(XSAN_RTL_CFLAGS ${SANITIZER_COMMON_CFLAGS} ${ASAN_CFLAGS} ${TSAN_RTL_CFLAGS})

set(XSAN_COMMON_DEFINITIONS "")

append_rtti_flag(OFF XSAN_RTL_CFLAGS)

# Silence warnings in system headers with MSVC.
if(NOT CLANG_CL)
  append_list_if(COMPILER_RT_HAS_EXTERNAL_FLAG "/experimental:external;/external:W0;/external:anglebrackets" XSAN_RTL_CFLAGS)
endif()

# Too many existing bugs, needs cleanup.
append_list_if(COMPILER_RT_HAS_WNO_FORMAT -Wno-format XSAN_RTL_CFLAGS)


################## Compilation setting from source files to object files ###############

if(NOT APPLE)
  add_compiler_rt_object_libraries(RTXsan
    ARCHS ${XSAN_SUPPORTED_ARCH}
    SOURCES ${XSAN_SOURCES}
    ADDITIONAL_HEADERS ${XSAN_HEADERS}
    CFLAGS ${XSAN_RTL_CFLAGS}
    DEFS ${XSAN_COMMON_DEFINITIONS})
  add_compiler_rt_object_libraries(RTXsan_cxx
    ARCHS ${XSAN_SUPPORTED_ARCH}
    SOURCES ${XSAN_CXX_SOURCES}
    ADDITIONAL_HEADERS ${XSAN_HEADERS}
    CFLAGS ${XSAN_RTL_CFLAGS}
    DEFS ${XSAN_COMMON_DEFINITIONS})
  add_compiler_rt_object_libraries(RTXsan_static
    ARCHS ${XSAN_SUPPORTED_ARCH}
    SOURCES ${XSAN_STATIC_SOURCES}
    ADDITIONAL_HEADERS ${XSAN_HEADERS}
    CFLAGS ${XSAN_RTL_CFLAGS}
    DEFS ${XSAN_COMMON_DEFINITIONS})
  add_compiler_rt_object_libraries(RTXsan_preinit
    ARCHS ${XSAN_SUPPORTED_ARCH}
    SOURCES ${XSAN_PREINIT_SOURCES}
    ADDITIONAL_HEADERS ${XSAN_HEADERS}
    CFLAGS ${XSAN_RTL_CFLAGS}
    DEFS ${XSAN_COMMON_DEFINITIONS})

  # file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp "")
  # add_compiler_rt_object_libraries(RTXsan_dynamic_version_script_dummy
  #   ARCHS ${XSAN_SUPPORTED_ARCH}
  #   SOURCES ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp
  #   CFLAGS ${XSAN_DYNAMIC_CFLAGS}
  #   DEFS ${XSAN_DYNAMIC_DEFINITIONS})
endif()

################### Set the LDFLAGS for the XSan runtime library ##########

set (WRAPPED_SYMBOLS ${CMAKE_CURRENT_SOURCE_DIR}/xsan_wrapped_symbols.txt.in)
# TODO: only add this file when ASan is activated.
append_list_if(True ${CMAKE_CURRENT_SOURCE_DIR}/asan/asan_wrapped_symbols.txt.in WRAPPED_SYMBOLS)
# TODO: only add this file when TSan is activated.
append_list_if(True ${CMAKE_CURRENT_SOURCE_DIR}/tsan/tsan_wrapped_symbols.txt.in WRAPPED_SYMBOLS)

# Located in src/cmake/AddWrapSyms.cmake
include(AddWrapSyms)
# Add the following task as a custom target:
#   Generate wrapped symbols file, which contains all symbols that need to be 
#   wrapped by _wrap=<symbol> linker option.
create_wrap_symbols_target(
  OUT ${XSAN_RESOURCE_DIR}/xsan_wrapped_symbols.txt
  IN ${WRAPPED_SYMBOLS}
)

set(XSAN_LINK_FLAGS "-Wl,@${XSAN_RESOURCE_DIR}/xsan_wrapped_symbols.txt")


################## Set the runtime library for the XSan runtime library ##########
if(MSVC)
  # asan on windows only supports the release dll version of the runtimes, in the interest of
  # only having one asan dll to support/test. Having asan statically linked
  # with the runtime might be possible, but it multiplies the number of scenerios to test.
  # the program USING sanitizers can use whatever version of the runtime it wants to.
  set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
endif()


################## Linking setting from object files to the static XSan runtime library ###############

# Build XSan runtimes shipped with Clang.
add_compiler_rt_component(xsan)

if(NOT APPLE)
  set(XSAN_COMMON_RUNTIME_OBJECT_LIBS
    RTInterception
    RTSanitizerCommon
    RTSanitizerCommonLibc
    RTSanitizerCommonCoverage
    RTSanitizerCommonSymbolizer
    RTSanitizerCommonSymbolizerInternal
    RTLSanCommon
    RTUbsan)

  if (NOT WIN32)
    add_compiler_rt_runtime(clang_rt.xsan
        STATIC
        ARCHS ${XSAN_SUPPORTED_ARCH}
        OBJECT_LIBS RTXsan_preinit
                    RTXsan          
                    ${XSAN_COMMON_RUNTIME_OBJECT_LIBS}
        CFLAGS ${XSAN_RTL_CFLAGS}
        LINK_FLAGS ${XSAN_LINK_FLAGS}
        DEFS ${XSAN_COMMON_DEFINITIONS}
        PARENT_TARGET xsan)

    add_compiler_rt_runtime(clang_rt.xsan_cxx
        STATIC
        ARCHS ${ASAN_SUPPORTED_ARCH}
        OBJECT_LIBS RTXsan_cxx
                    RTUbsan_cxx
        CFLAGS ${XSAN_RTL_CFLAGS}
        LINK_FLAGS ${XSAN_LINK_FLAGS}
        DEFS ${XSAN_COMMON_DEFINITIONS}
        PARENT_TARGET xsan)
    
    add_compiler_rt_runtime(clang_rt.xsan_static
        STATIC
        ARCHS ${XSAN_SUPPORTED_ARCH}
        OBJECT_LIBS RTXsan_static
        CFLAGS ${XSAN_RTL_CFLAGS}
        LINK_FLAGS ${XSAN_LINK_FLAGS}
        DEFS ${XSAN_COMMON_DEFINITIONS}
        PARENT_TARGET xsan)
    
    add_compiler_rt_runtime(clang_rt.xsan-preinit
        STATIC
        ARCHS ${XSAN_SUPPORTED_ARCH}
        OBJECT_LIBS RTXsan_preinit
        CFLAGS ${XSAN_RTL_CFLAGS}
        LINK_FLAGS ${XSAN_LINK_FLAGS}
        DEFS ${XSAN_COMMON_DEFINITIONS}
        PARENT_TARGET xsan)
  endif()

  ################ Dump the extern symbols in dynamic libraries ###############
  foreach(arch ${XSAN_SUPPORTED_ARCH})
    if (SANITIZER_USE_SYMBOLS AND NOT ${arch} STREQUAL "i386")
      add_sanitizer_rt_symbols(clang_rt.xsan_cxx
        ARCHS ${arch}
        EXTRA xsan.syms.extra)
      add_sanitizer_rt_symbols(clang_rt.xsan
        ARCHS ${arch}
        EXTRA xsan.syms.extra)
      add_dependencies(xsan clang_rt.xsan-${arch}-symbols
                            clang_rt.xsan_cxx-${arch}-symbols)
    endif()

  endforeach()
endif()