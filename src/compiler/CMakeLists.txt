################# For clang_wrapper.c #################

add_executable(xclang clang_wrapper.c xsan_wrapper_helper.c)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # eliminate warn of `u8* to char*`, which is applied generally in clang_wrapper.c
    target_compile_options(xclang PRIVATE -Wno-pointer-sign)
endif()

include_directories(include)

# set output directory
set_target_properties(xclang
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${XSAN_BIN_DIR}"
)

add_custom_command(TARGET xclang POST_BUILD
    COMMAND ln -sf "xclang" "${XSAN_BIN_DIR}/xclang++")


    
include_directories(${CMAKE_CURRENT_BINARY_DIR})
configure_file(config_compile.h.in config_compile.h)
    
################# For HotPatch #################
add_subdirectory(livepatch)
