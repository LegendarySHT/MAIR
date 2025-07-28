########## Set the sources for the XSan runtime library ##########

# Build for the XSan runtime support library.

# we now cannot support disable ASan, we can raise a cmake warning when detecting XSAN_CONTAINS_ASAN is OFF, and then set it to ON.
if(DEFINED XSAN_CONTAINS_ASAN AND NOT XSAN_CONTAINS_ASAN)
  message(WARNING
    "XSAN_CONTAINS_ASAN is OFF, but disabling ASan is currently not supported.\n"
    "ASan will be forcibly enabled. Support for disabling it may be added in the future."
  )
  set(XSAN_CONTAINS_ASAN ON CACHE BOOL "Enable AddressSanitizer (ASan) globally" FORCE)
endif()

# Import sub-sanitizer under `dir`
# output variables:
# - ${output_prefix}_HEADERS
# - ${output_prefix}_INCLUDE_DIRS
# - ${output_prefix}_SOURCES
# - ${output_prefix}_COND_SOURCES (sources using the XSAN_CONTAINS_* condition)
# - ${output_prefix}_STATIC_SOURCES
# - ${output_prefix}_CFLAGS
# - ${output_prefix}_DYNAMIC_CFLAGS
# - ${output_prefix}_COMMON_DEFINITIONS
# - ${output_prefix}_DYNAMIC_DEFINITIONS
# - ${output_prefix}_WRAPPED_SYMBOLS
# - ${output_prefix}_LINK_LIBS
macro(import_subsan_wrapper dir output_prefix)
  unset(import_subsan)
  add_subdirectory(${dir})
  # imported from the subdirectory
  import_subsan(${output_prefix})

  transform_list_in_place(${output_prefix}_HEADERS PREPEND "${dir}/")
  transform_list_in_place(${output_prefix}_INCLUDE_DIRS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/")
  transform_list_in_place(${output_prefix}_SOURCES PREPEND "${dir}/")
  transform_list_in_place(${output_prefix}_COND_SOURCES PREPEND "${dir}/")
  transform_list_in_place(${output_prefix}_STATIC_SOURCES PREPEND "${dir}/")
  transform_list_in_place(${output_prefix}_WRAPPED_SYMBOLS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/")
endmacro()

######################## Headers #########################

SET(XSAN_HEADERS
  xsan_activation.h
  xsan_allocator.h
  xsan_attribute.h
  xsan_flags.h
  xsan_hooks.h
  xsan_hooks_default.h
  xsan_hooks_dispatch.h
  xsan_hooks_gen.h
  xsan_hooks_types.h
  xsan_interceptors.h
  xsan_interceptors_memintrinsics.h
  xsan_interface_internal.h
  xsan_internal.h
  xsan_stack.h
  xsan_stack_interface.h
  xsan_thread.h
)

set(XSAN_INCLUDE_DIRS
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../
)

######################## Sources #########################

set(XSAN_SOURCES)

# sources using the XSAN_CONTAINS_* condition
set(XSAN_COND_SOURCES
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
  xsan_stack.cpp
  xsan_thread.cpp
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
  xsan_rtl_static.cpp
)

set(XSAN_PREINIT_SOURCES
  xsan_preinit.cpp
)

######################## Flags #########################

set(XSAN_CFLAGS ${SANITIZER_COMMON_CFLAGS})

append_rtti_flag(OFF XSAN_CFLAGS)

# Silence warnings in system headers with MSVC.
if(NOT CLANG_CL)
  append_list_if(COMPILER_RT_HAS_EXTERNAL_FLAG "/experimental:external;/external:W0;/external:anglebrackets" XSAN_CFLAGS)
endif()

# Too many existing bugs, needs cleanup.
append_list_if(COMPILER_RT_HAS_WNO_FORMAT -Wno-format XSAN_CFLAGS)

set(XSAN_DYNAMIC_CFLAGS ${SANITIZER_COMMON_CFLAGS})
list(REMOVE_ITEM XSAN_DYNAMIC_CFLAGS -fPIE)

######################## Definitions #########################

set(XSAN_COMMON_DEFINITIONS)
set(XSAN_DYNAMIC_DEFINITIONS XSAN_DYNAMIC=1)

######################## Link Libraries #########################

set(XSAN_WRAPPED_SYMBOLS ${CMAKE_CURRENT_SOURCE_DIR}/xsan_wrapped_symbols.txt.in)
set(XSAN_LINK_LIBS)

#################### Import Sub-san ####################

set(INCLUDE_DIRS ${XSAN_INCLUDE_DIRS})

foreach(San IN LISTS XSAN_DELEGATED_SANITIZERS)
  string(TOLOWER "${San}" san)
  string(TOUPPER "${San}" SAN)
  import_subsan_wrapper(${san} ${SAN})
  list(APPEND INCLUDE_DIRS ${${SAN}_INCLUDE_DIRS})
endforeach()

include_directories(${INCLUDE_DIRS})

# Located in src/cmake/AddWrapSyms.cmake, to use create_wrap_symbols_target
include(AddWrapSyms)

if(MSVC)
  # asan on windows only supports the release dll version of the runtimes, in the interest of
  # only having one asan dll to support/test. Having asan statically linked
  # with the runtime might be possible, but it multiplies the number of scenerios to test.
  # the program USING sanitizers can use whatever version of the runtime it wants to.
  set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
endif()

######################## LDFLAGS #########################

set(XSAN_COMMON_LINK_FLAGS)
set(XSAN_COMMON_DYNAMIC_LINK_FLAGS ${SANITIZER_COMMON_LINK_FLAGS})

if(ANDROID)
  # Put most Sanitizer shared libraries in the global group. For more details, see
  # android-changes-for-ndk-developers.md#changes-to-library-search-order
  if (COMPILER_RT_HAS_Z_GLOBAL)
    list(APPEND XSAN_COMMON_DYNAMIC_LINK_FLAGS -Wl,-z,global)
  endif()
endif()
# LLVM turns /OPT:ICF back on when LLVM_ENABLE_PDBs is set
# we _REALLY_ need to turn it back off for ASAN, because the way
# asan emulates weak functions from DLLs requires NOICF
append_list_if(MSVC "LINKER:/DEBUG;LINKER:/OPT:NOICF" XSAN_COMMON_DYNAMIC_LINK_FLAGS)

################## Compilation setting from source files to object files ###############

# Compile common libraries. (cxx, preinit)
if(NOT APPLE)
  add_compiler_rt_object_libraries(RTXsan_cxx
    ARCHS ${XSAN_SUPPORTED_ARCH}
    SOURCES ${XSAN_CXX_SOURCES}
    ADDITIONAL_HEADERS ${XSAN_HEADERS}
    CFLAGS ${XSAN_CFLAGS}
    DEFS ${XSAN_COMMON_DEFINITIONS})

  add_compiler_rt_object_libraries(RTXsan_preinit
    ARCHS ${XSAN_SUPPORTED_ARCH}
    SOURCES ${XSAN_PREINIT_SOURCES}
    ADDITIONAL_HEADERS ${XSAN_HEADERS}
    CFLAGS ${XSAN_CFLAGS}
    DEFS ${XSAN_COMMON_DEFINITIONS})

  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp "")
  add_compiler_rt_object_libraries(RTXsan_dynamic_version_script_dummy
    ARCHS ${XSAN_SUPPORTED_ARCH}
    SOURCES ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp
    CFLAGS ${XSAN_DYNAMIC_CFLAGS}
    DEFS ${XSAN_DYNAMIC_DEFINITIONS})
endif()

# Compile individual parts of each sanitizer. (main lib, dynamic, static)
# Conditional parts are compiled when generating certain combinations.
set(XSAN_OBJ_SAN_LIST ${XSAN_DELEGATED_SANITIZERS} Xsan)
foreach(San IN LISTS XSAN_OBJ_SAN_LIST)
  string(TOLOWER "${San}" san)
  string(TOUPPER "${San}" SAN)

  add_compiler_rt_object_libraries(RTXsan_dynamic_${san}
    OS ${SANITIZER_COMMON_SUPPORTED_OS}
    ARCHS ${XSAN_SUPPORTED_ARCH}
    SOURCES ${${SAN}_SOURCES} ${${SAN}_CXX_SOURCES}
    ADDITIONAL_HEADERS ${${SAN}_HEADERS}
    CFLAGS ${${SAN}_DYNAMIC_CFLAGS}
    DEFS ${${SAN}_DYNAMIC_DEFINITIONS})

  if(NOT APPLE)
    add_compiler_rt_object_libraries(RTXsan_${san}
      ARCHS ${XSAN_SUPPORTED_ARCH}
      SOURCES ${${SAN}_SOURCES}
      ADDITIONAL_HEADERS ${${SAN}_HEADERS}
      CFLAGS ${${SAN}_CFLAGS}
      DEFS ${${SAN}_COMMON_DEFINITIONS})
    if(NOT "${${SAN}_STATIC_SOURCES}" STREQUAL "")
      add_compiler_rt_object_libraries(RTXsan_static_${san}
        ARCHS ${XSAN_SUPPORTED_ARCH}
        SOURCES ${${SAN}_STATIC_SOURCES}
        ADDITIONAL_HEADERS ${${SAN}_HEADERS}
        CFLAGS ${${SAN}_CFLAGS}
        DEFS ${${SAN}_COMMON_DEFINITIONS})
    endif()
  endif()
endforeach()

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

  # Generate common libraries. (cxx, preinit)
  if (NOT WIN32)
    add_compiler_rt_runtime(clang_rt.xsan_cxx
        STATIC
        ARCHS ${XSAN_SUPPORTED_ARCH}
        OBJECT_LIBS RTXsan_cxx
                    RTUbsan_cxx
        CFLAGS ${XSAN_CFLAGS}
        LINK_FLAGS ${XSAN_COMMON_LINK_FLAGS}
        PARENT_TARGET xsan)

    add_compiler_rt_runtime(clang_rt.xsan-preinit
        STATIC
        ARCHS ${XSAN_SUPPORTED_ARCH}
        OBJECT_LIBS RTXsan_preinit
        CFLAGS ${XSAN_CFLAGS}
        LINK_FLAGS ${XSAN_COMMON_LINK_FLAGS}
        PARENT_TARGET xsan)
  endif()

  # TODO: Support power set
  set(XSAN_STATIC_DEMO_COMB ${XSAN_DELEGATED_SANITIZERS})
  set(XSAN_DYNAMIC_DEMO_COMB ${XSAN_DELEGATED_SANITIZERS})
  list(REMOVE_ITEM XSAN_DYNAMIC_DEMO_COMB Msan)
  set(XSAN_STATIC_COMBINATIONS XSAN_STATIC_DEMO_COMB)
  set(XSAN_DYNAMIC_COMBINATIONS XSAN_DYNAMIC_DEMO_COMB)

  ################ Generate static libraries ##################################
  foreach(comb IN LISTS XSAN_STATIC_COMBINATIONS)
    set(XSAN_OBJ_LIST)
    set(XSAN_STATIC_OBJ_LIST)
    set(XSAN_CFLAGS_LIST)
    set(XSAN_WRAPPED_SYMBOLS_LIST)

    set(obj_san_list ${${comb}} Xsan)
    # Collect information of this combination
    foreach(San IN LISTS obj_san_list)
      string(TOLOWER "${San}" san)
      string(TOUPPER "${San}" SAN)
      list(APPEND XSAN_OBJ_LIST RTXsan_${san})
      if(NOT "${${SAN}_STATIC_SOURCES}" STREQUAL "")
        list(APPEND XSAN_STATIC_OBJ_LIST RTXsan_static_${san})
      endif()
      list(APPEND XSAN_CFLAGS_LIST ${${SAN}_CFLAGS})
      list(APPEND XSAN_WRAPPED_SYMBOLS_LIST ${${SAN}_WRAPPED_SYMBOLS})

      if(NOT "${${SAN}_COND_SOURCES}" STREQUAL "")
        # Compile parts using the XSAN_CONTAINS_* condition
        add_compiler_rt_object_libraries(RTXsan_cond_${san}
          ARCHS ${XSAN_SUPPORTED_ARCH}
          SOURCES ${${SAN}_COND_SOURCES}
          ADDITIONAL_HEADERS ${${SAN}_HEADERS}
          CFLAGS ${${SAN}_CFLAGS}
          DEFS ${${SAN}_COMMON_DEFINITIONS})
        list(APPEND XSAN_OBJ_LIST RTXsan_cond_${san})
      endif()
    endforeach()

    # message(STATUS "comb: ${${comb}}")
    # message(STATUS "XSAN_OBJ_LIST: ${XSAN_OBJ_LIST}")
    # message(STATUS "XSAN_STATIC_OBJ_LIST: ${XSAN_STATIC_OBJ_LIST}")
    # message(STATUS "XSAN_CFLAGS_LIST: ${XSAN_CFLAGS_LIST}")
    # message(STATUS "XSAN_WRAPPED_SYMBOLS_LIST: ${XSAN_WRAPPED_SYMBOLS_LIST}")

    # Add the following task as a custom target:
    #   Generate wrapped symbols file, which contains all symbols that need to be
    #   wrapped by _wrap=<symbol> linker option.
    # TODO: When supporting more combinations, we need to rename the file name
    create_wrap_symbols_target(
      generate_wrap_symbols
      OUT ${XSAN_DATA_DIR}/xsan_wrapped_symbols.txt
      IN ${XSAN_WRAPPED_SYMBOLS_LIST}
    )
    set(XSAN_COMMON_DEPS generate_wrap_symbols)
    set(XSAN_LINK_FLAGS
      ${XSAN_COMMON_LINK_FLAGS}
      "-Wl,@${XSAN_DATA_DIR}/xsan_wrapped_symbols.txt"
    )

    # Generate combinations of sanitizers. (main lib, static)
    if (NOT WIN32)
      add_compiler_rt_runtime(clang_rt.xsan
          STATIC
          ARCHS ${XSAN_SUPPORTED_ARCH}
          OBJECT_LIBS RTXsan_preinit
                      ${XSAN_OBJ_LIST}
                      ${XSAN_COMMON_RUNTIME_OBJECT_LIBS}
          CFLAGS ${XSAN_CFLAGS_LIST}
          LINK_FLAGS ${XSAN_LINK_FLAGS}
          DEPS ${XSAN_COMMON_DEPS}
          PARENT_TARGET xsan)

      add_compiler_rt_runtime(clang_rt.xsan_static
          STATIC
          ARCHS ${XSAN_SUPPORTED_ARCH}
          OBJECT_LIBS ${XSAN_STATIC_OBJ_LIST}
          CFLAGS ${XSAN_CFLAGS_LIST}
          LINK_FLAGS ${XSAN_LINK_FLAGS}
          DEPS ${XSAN_COMMON_DEPS}
          PARENT_TARGET xsan)
    endif()
  endforeach()

  ################ Generate dynamic libraries ##################################
  foreach(comb IN LISTS XSAN_DYNAMIC_COMBINATIONS)
    set(XSAN_HEADER_LIST)
    set(XSAN_DYNAMIC_OBJ_LIST)
    set(XSAN_DYNAMIC_CFLAGS_LIST)
    set(XSAN_LINK_LIBS_LIST)

    set(obj_san_list ${${comb}} Xsan)
    # Collect information of this combination
    foreach(San IN LISTS obj_san_list)
      string(TOLOWER "${San}" san)
      string(TOUPPER "${San}" SAN)
      list(APPEND XSAN_HEADER_LIST ${${SAN}_HEADERS})
      list(APPEND XSAN_DYNAMIC_OBJ_LIST RTXsan_dynamic_${san})
      list(APPEND XSAN_DYNAMIC_CFLAGS_LIST ${${SAN}_DYNAMIC_CFLAGS})
      list(APPEND XSAN_LINK_LIBS_LIST ${${SAN}_LINK_LIBS})

      if(NOT "${${SAN}_COND_SOURCES}" STREQUAL "")
        # Compile parts using the XSAN_CONTAINS_* condition
        add_compiler_rt_object_libraries(RTXsan_dynamic_cond_${san}
          OS ${SANITIZER_COMMON_SUPPORTED_OS}
          ARCHS ${XSAN_SUPPORTED_ARCH}
          SOURCES ${${SAN}_COND_SOURCES}
          ADDITIONAL_HEADERS ${${SAN}_HEADERS}
          CFLAGS ${${SAN}_DYNAMIC_CFLAGS}
          DEFS ${${SAN}_DYNAMIC_DEFINITIONS})
        list(APPEND XSAN_DYNAMIC_OBJ_LIST RTXsan_dynamic_cond_${san})
      endif()
    endforeach()

    # message(STATUS "comb: ${${comb}}")
    # message(STATUS "XSAN_HEADER_LIST: ${XSAN_HEADER_LIST}")
    # message(STATUS "XSAN_DYNAMIC_OBJ_LIST: ${XSAN_DYNAMIC_OBJ_LIST}")
    # message(STATUS "XSAN_DYNAMIC_CFLAGS_LIST: ${XSAN_DYNAMIC_CFLAGS_LIST}")
    # message(STATUS "XSAN_LINK_LIBS_LIST: ${XSAN_LINK_LIBS_LIST}")

    # TODO: generate the wrapped symbols file when it doesn't exist.
    set(XSAN_DYNAMIC_LINK_FLAGS
      ${XSAN_COMMON_DYNAMIC_LINK_FLAGS}
      "-Wl,@${XSAN_DATA_DIR}/xsan_wrapped_symbols.txt"
    )

    ################ Dump the extern symbols in dynamic libraries ###############
    foreach(arch ${XSAN_SUPPORTED_ARCH})
      if (COMPILER_RT_HAS_VERSION_SCRIPT)
        if(WIN32)
          set(SANITIZER_RT_VERSION_LIST_LIBS clang_rt.xsan-${arch})
        else()
          set(SANITIZER_RT_VERSION_LIST_LIBS clang_rt.xsan-${arch} clang_rt.xsan_cxx-${arch})
        endif()
        add_sanitizer_rt_version_list(clang_rt.xsan-dynamic-${arch}
                                      LIBS ${SANITIZER_RT_VERSION_LIST_LIBS}
                                      EXTRA xsan.syms.extra)
        set(VERSION_SCRIPT_FLAG
            -Wl,--version-script,${CMAKE_CURRENT_BINARY_DIR}/clang_rt.xsan-dynamic-${arch}.vers)
        # The Solaris 11.4 linker supports a subset of GNU ld version scripts,
        # but requires a special option to enable it.
        if (COMPILER_RT_HAS_GNU_VERSION_SCRIPT_COMPAT)
            list(APPEND VERSION_SCRIPT_FLAG -Wl,-z,gnu-version-script-compat)
        endif()
        set_property(SOURCE
          ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp
          APPEND PROPERTY
          OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/clang_rt.xsan-dynamic-${arch}.vers)
      else()
        set(VERSION_SCRIPT_FLAG)
      endif()

      # Generate combinations of sanitizers. (dynamic)
      add_compiler_rt_runtime(clang_rt.xsan
        SHARED
        ARCHS ${arch}
        OBJECT_LIBS ${XSAN_COMMON_RUNTIME_OBJECT_LIBS}
                    ${XSAN_DYNAMIC_OBJ_LIST}
                    # The only purpose of RTAsan_dynamic_version_script_dummy is to
                    # carry a dependency of the shared runtime on the version script.
                    # Replacing it with a straightforward
                    # add_dependencies(clang_rt.asan-dynamic-${arch} clang_rt.asan-dynamic-${arch}-version-list)
                    # generates an order-only dependency in ninja.
                    RTXsan_dynamic_version_script_dummy
                    RTUbsan_cxx
        ADDITIONAL_HEADERS ${XSAN_HEADER_LIST}
        CFLAGS ${XSAN_DYNAMIC_CFLAGS_LIST}
        LINK_FLAGS ${XSAN_DYNAMIC_LINK_FLAGS}
                  ${VERSION_SCRIPT_FLAG}
        LINK_LIBS ${XSAN_LINK_LIBS_LIST}
        PARENT_TARGET xsan)

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
  endforeach()
endif()


# generate xsan_hooks_gen.h automatically
set(CONFIG_FILE_PATH "${CMAKE_SOURCE_DIR}/src/runtime/lib/xsan/xsan_hooks_gen.cmake")

if(EXISTS ${CONFIG_FILE_PATH})
  include(${CONFIG_FILE_PATH})
else()
  message(STATUS "Configuration file not found, can not auto-generate the xsan_hooks_gen.h file.")
endif()
