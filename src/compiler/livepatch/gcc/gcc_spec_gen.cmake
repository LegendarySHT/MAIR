# # Generate XSan cleaned specs from both g++ and gcc using a shared cleaner

# Use the standalone Python script for sanitizer block removal
set(DUMP_GCC_SPEC_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/dump_gcc_spec.py")

# Python script has been moved to dump_gcc_spec.py for better maintainability

# Ensure spec filenames are available (fallback to defaults if not set by parent)
if(NOT DEFINED XSAN_SPEC_GPP_NAME)
    set(XSAN_SPEC_GPP_NAME "xsan.gpp.spec")
endif()

if(NOT DEFINED XSAN_SPEC_GCC_NAME)
    set(XSAN_SPEC_GCC_NAME "xsan.gcc.spec")
endif()

function(generate_clean_spec DRIVER EXEC_NAME OUT_SPEC_BASENAME)
    # Find Python 3 interpreter
    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    # Use the new dump_gcc_spec.py script to generate cleaned specs
    set(XSAN_SPEC_FILE "${XSAN_OUTPUT_DATADIR}/${OUT_SPEC_BASENAME}")
    file(MAKE_DIRECTORY "${XSAN_OUTPUT_DATADIR}")

    execute_process(
        COMMAND ${Python3_EXECUTABLE} "${DUMP_GCC_SPEC_SCRIPT}" --cc=${DRIVER} -o "${XSAN_SPEC_FILE}"
        RESULT_VARIABLE PYTHON_RESULT
    )

    if(NOT PYTHON_RESULT EQUAL 0)
        message(FATAL_ERROR "${EXEC_NAME}: Failed to generate cleaned spec with dump_gcc_spec.py")
    endif()

    message(STATUS "Generated XSan spec file: ${XSAN_SPEC_FILE}")

    # Install the spec file
    install(FILES "${XSAN_SPEC_FILE}"
        DESTINATION ${XSAN_INSTALL_DATADIR}
        COMPONENT resource
    )
endfunction()

generate_clean_spec(g++ gpp ${XSAN_SPEC_GPP_NAME})
generate_clean_spec(gcc gcc ${XSAN_SPEC_GCC_NAME})
