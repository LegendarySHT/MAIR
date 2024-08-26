add_executable(xclang clang_wrapper.c)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # eliminate warn of `u8* to char*`, which is applied generally in clang-wrapper.c
    target_compile_options(xclang PRIVATE -Wno-pointer-sign)
endif()

include_directories(include)

set_target_properties(xclang
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${XSAN_BIN_DIR}"
)

add_custom_command(TARGET xclang POST_BUILD
    COMMAND ln -sf "xclang" "${XSAN_BIN_DIR}/xclang++")
