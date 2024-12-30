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

    foreach(in_file IN LISTS in_files)
        if(EXISTS "${in_file}")
            file(READ "${in_file}" content)
            # Remove carriage return (for Windows-style files)
            string(REGEX REPLACE "\r" "" content "${content}")
            # Split into list by line
            string(REGEX REPLACE "\n+" ";" symbols "${content}")
            # Remove empty items
            list(REMOVE_ITEM symbols "")
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