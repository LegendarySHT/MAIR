include_directories(include)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
configure_file(config_compile.h.in config_compile.h)

# ################ For clang_wrapper.c #################
add_executable(xclang clang_wrapper.c)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  # eliminate warn of `u8* to char*`, which is applied generally in clang_wrapper.c
  target_compile_options(xclang PRIVATE -Wno-pointer-sign)
endif()

# set output directory
set_target_properties(xclang
  PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${XSAN_BIN_DIR}"
)

add_custom_command(TARGET xclang POST_BUILD
  COMMAND ln -sf "xclang" "${XSAN_BIN_DIR}/xclang++")

# ################ For gcc_wrapper.c  #################
add_executable(xgcc gcc_wrapper.c)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  # eliminate warn of `u8* to char*`, which is applied generally in clang_gcc.c
  target_compile_options(xgcc PRIVATE -Wno-pointer-sign)
endif()

set_target_properties(xgcc
  PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${XSAN_BIN_DIR}"
)

add_custom_command(TARGET xgcc POST_BUILD
  COMMAND ln -sf "xgcc" "${XSAN_BIN_DIR}/xg++")

# ################ For gcc unsupport sanitizer #################
set(GCC_UNSUPPORTED_SANITIZERS ${XSAN_SANITIZERS})
list(REMOVE_ITEM GCC_UNSUPPORTED_SANITIZERS ${GCC_SUPPORT_SANITIZERS})

foreach(unsupported_san IN LISTS GCC_UNSUPPORTED_SANITIZERS)
  target_compile_definitions(xgcc PRIVATE "COMPILER_UNSUPPORT_${unsupported_san}=1")
endforeach()

# ################ For clang unsupport sanitizer #################
set(CLANG_UNSUPPORTED_SANITIZERS ${XSAN_SANITIZERS})
list(REMOVE_ITEM CLANG_UNSUPPORTED_SANITIZERS ${CLANG_SUPPORT_SANITIZERS})

foreach(unsupported_san IN LISTS CLANG_UNSUPPORTED_SANITIZERS)
  target_compile_definitions(xclang PRIVATE "COMPILER_UNSUPPORT_${unsupported_san}=1")
endforeach()

# ################ For HotPatch #################
add_subdirectory(livepatch)
