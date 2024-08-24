add_executable(xclang clang_wrapper.c)

include_directories(include)

set_target_properties(xclang
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${XSAN_BIN_DIR}"
)

add_custom_command(TARGET xclang POST_BUILD
    COMMAND ln -sf "xclang" "${XSAN_BIN_DIR}/xclang++")
