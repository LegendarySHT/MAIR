# This file is used as either script file or a cmake module. 
# For script file usage in cmake --build stage, it is executed by 
#     "cmake -P AddWrapSyms.cmake -DIN=${inputs} -DOUT=${output}"
# For module usage, it is included by 
#     "include(AddWrapSyms)" 
# in the CMakeLists.txt file.

set(THIS_FILE "${CMAKE_CURRENT_LIST_FILE}")

function(generate_wrap_symbols)
    # Use cmake_parse_arguments to parse named arguments
    cmake_parse_arguments(GWS
        ""        # No options
        "OUT"     # Sole value option (OUT)
        "IN"      # Multiple value option (IN)
        ${ARGN}   # Remaining arguments
    )
    # Check if OUT parameter is provided
    if(NOT GWS_OUT)
        message(FATAL_ERROR "generate_wrap_symbols: OUT parameter is required.")
    endif()

    # Check if IN parameter is provided
    if(NOT GWS_IN)
        message(FATAL_ERROR "generate_wrap_symbols: IN parameter is required.")
    endif()

    set(out_file "${GWS_OUT}")
    set(in_files "${GWS_IN}")

    set(wrap_flags "")
    cmake_policy(PUSH)
    # Suppress CMP0007 warning locally for CMake 3.20 and later
    cmake_policy(SET CMP0007 NEW)
    foreach(in_file IN LISTS in_files)
        if(EXISTS "${in_file}")
            file(READ "${in_file}" content)
            # Remove carriage return (for Windows-style files)
            string(REGEX REPLACE "\r" "" content "${content}")
            # Split into list by line
            string(REGEX REPLACE "\n+" ";" symbols "${content}")
            # Remove empty items
            list(FILTER symbols EXCLUDE REGEX "^\\s*$")
            # Remove comments start with "#"
            list(FILTER symbols EXCLUDE REGEX "^#.*")
            foreach(symbol IN LISTS symbols)
                string(STRIP "${symbol}" symbol_stripped)
                if(NOT symbol_stripped STREQUAL "")
                    list(APPEND wrap_flags "--wrap=${symbol_stripped}")
                endif()
            endforeach()
        else()
            message(WARNING "Symbol file ${in_file} does not exist.")
        endif()
    endforeach()
    cmake_policy(POP)

    # Remove duplicates
    list(REMOVE_DUPLICATES wrap_flags)

    # Write output file
    file(WRITE "${out_file}" "") # clear file
    foreach(flag IN LISTS wrap_flags)
        file(APPEND "${out_file}" "${flag}\n")
    endforeach()

    if(in_files)
        # Format input file list for display
        list(JOIN in_files "\n          " formatted_in_files)
        message(STATUS "Generated wrapped symbols file ${out_file}\n    with  ${formatted_in_files}")
    else()
        message(STATUS "Generated empty wrapped symbols file ${out_file} with no input files.")
    endif()
endfunction()

function(create_wrap_symbols_target TARGET_NAME)
    # Use cmake_parse_arguments to parse named arguments
    cmake_parse_arguments(GWS
        ""        # No options
        "OUT"     # Sole value option (OUT)
        "IN"      # Multiple value option (IN)
        ${ARGN}   # Remaining arguments
    )
    # Check if OUT parameter is provided
    if(NOT GWS_OUT)
        message(FATAL_ERROR "generate_wrap_symbols: OUT parameter is required.")
    endif()

    # Check if IN parameter is provided
    if(NOT GWS_IN)
        message(FATAL_ERROR "generate_wrap_symbols: IN parameter is required.")
    endif()

    set(out_file "${GWS_OUT}")
    set(in_files "${GWS_IN}")

    # # Create target to generate wrapped symbols file
    # add_custom_target(generate_wrap_symbols ALL
    #     # Add output file
    #     COMMAND ${CMAKE_COMMAND} -DOUT="${out_file}" -DIN="${in_files}" -P "${THIS_FILE}"
    #     COMMENT "Generating wrapped symbols file ${output_file}"
    #     DEPENDS ${in_files}  # input_files are dependencies of the target
    # )

    # Define a custom command to generate the output file
    add_custom_command(
        OUTPUT ${out_file}
        COMMAND ${CMAKE_COMMAND} -DOUT=${out_file} -DIN="${in_files}" -P "${THIS_FILE}"
        COMMENT "Generating wrapped symbols file ${out_file}"
        DEPENDS ${in_files}  # Input files as dependencies
    )
    add_custom_target(${TARGET_NAME} ALL
        DEPENDS ${out_file}
    )

    message_green("TARGET_NAME: ${TARGET_NAME}")

    set(${TARGET_NAME} ${TARGET_NAME} PARENT_SCOPE)

    if(in_files)
        # Format input file list for display
        list(JOIN in_files "\n          " formatted_in_files)
        message(STATUS "Ready to generate wrapped symbols file ${out_file}\n    with  ${formatted_in_files}")
    else()
        message(STATUS "Ready to generate empty wrapped symbols file ${out_file} with no input files.")
    endif()

    # Install the wrapped symbols file to ${XSAN_INSATLL_DATADIR}
    install(FILES ${out_file}
        DESTINATION ${XSAN_INSATLL_DATADIR}
        COMPONENT resource
    )
endfunction()

# If the AddWrapSyms.cmake is used by cmake -P <this_file>, 
# not included by include() in CMakeLists.txt,
# then the following code will be executed.
message(STATUS "Executing ${THIS_FILE} as script file.")
if(CMAKE_SCRIPT_MODE_FILE)
    # Compare wether the TWO paths are the same
    if(NOT "${CMAKE_CURRENT_LIST_FILE}" STREQUAL "${THIS_FILE}")
        message(FATAL_ERROR "AddWrapSyms.cmake should be included by include() in CMakeLists.txt, not executed by -P.")
    endif()
    # Resolve ${IN}="aaa bbb ccc" into a list
    string(REPLACE " " ";" IN_LIST ${IN})
    # Call generate_wrap_symbols() with the resolved list
    generate_wrap_symbols(OUT ${OUT} IN ${IN_LIST})
endif()