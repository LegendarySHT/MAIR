# -------------------------------------------------------------------
# 1. Check LLVM version
# -------------------------------------------------------------------

find_package(LLVM REQUIRED CONFIG)

if(NOT LLVM_FOUND)
  message(FATAL_ERROR
    "find_package(LLVM) failed, please ensure LLVM_DIR points to the cmake config directory of LLVM 15:\n"
    "  e.g. -DLLVM_DIR=/path/to/llvm-15"
  )
endif()

string(REGEX MATCH "^15\\." _llvm15_ok "${LLVM_PACKAGE_VERSION}")

if(NOT _llvm15_ok)
  message(FATAL_ERROR
    "Detected LLVM version ${LLVM_PACKAGE_VERSION}, but XSan requires LLVM 15.\n"
    "Please specify LLVM15 via -DLLVM_DIR=/path/to/llvm-15"
  )
endif()

message_green("Using LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")

# -------------------------------------------------------------------
# 2. If the current compiler is not Clang 15, search and verify the version in order
# -------------------------------------------------------------------
function(derive_clangxx clang_bin OUT_CXX_PATH_VAR)
  # 1. Decompose the input clang path into directory and filename
  get_filename_component(CLANG_DIR "${clang_bin}" DIRECTORY)
  get_filename_component(CLANG_FILENAME "${clang_bin}" NAME)

  # 2. Check if the filename starts with "clang", if not, it cannot be processed
  if(NOT CLANG_FILENAME MATCHES "^clang")
    message(WARNING "Input path '${clang_bin}' does not appear to be a clang compiler.")
    set(${OUT_CXX_PATH_VAR} "" PARENT_SCOPE)
    return()
  endif()

  # 3. Use regex to replace "clang" with "clang++" in the filename
  # ^clang means only replace the "clang" at the beginning of the string
  string(REGEX REPLACE "^clang" "clang++" CLANG_PP_FILENAME "${CLANG_FILENAME}")

  # 4. Combine the full candidate path of clang++
  # If CLANG_DIR is empty (e.g. input is "clang"), the result should be just "clang++" 
  # (no leading slash)
  if(CLANG_DIR STREQUAL "")
    set(CANDIDATE_CXX_PATH "${CLANG_PP_FILENAME}")
  else()
    set(CANDIDATE_CXX_PATH "${CLANG_DIR}/${CLANG_PP_FILENAME}")
  endif()

  # 5. Verify if the candidate path exists and is an executable file
  if(EXISTS "${CANDIDATE_CXX_PATH}")
    # If verified, set the found path to the output variable
    set(${OUT_CXX_PATH_VAR} "${CANDIDATE_CXX_PATH}" PARENT_SCOPE)
  else()
    # If verification fails, set the output variable to an empty string
    set(${OUT_CXX_PATH_VAR} "" PARENT_SCOPE)
  endif()
endfunction()

function(find_and_verify_clang15 out_var)
  set(_candidates

    # Try ${LLVM_TOOLS_BINARY_DIR}/clang first
    ${LLVM_TOOLS_BINARY_DIR}/clang

    # Then try clang-15 in the system
    clang-15

    # Finally fallback to clang
    clang
  )

  foreach(_cmd IN LISTS _candidates)
    message_green("\tChecking ${_cmd} ...")

    # Only check the existence of the executable file
    find_program(_path NAMES ${_cmd})

    if(NOT _path)
      # If the input is a file path, also try to check the executable file directly
      if(IS_ABSOLUTE "${_cmd}" AND EXISTS "${_cmd}" AND NOT IS_DIRECTORY "${_cmd}")
        set(_path "${_cmd}")
      endif()
    endif()

    if(NOT _path)
      continue()
    endif()

    # Execute --version, parse the major version
    execute_process(
      COMMAND "${_path}" --version
      OUTPUT_VARIABLE _ver_out
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    # The version line of Clang is usually "clang version 15.x.y ..." or "Ubuntu clang version 15..."
    string(REGEX MATCH "([Vv]ersion[[:space:]]+)?15([\\.[:digit:]]*)" _match "${_ver_out}")

    if(NOT _match)
      continue()
    endif()

    derive_clangxx("${_path}" _clang15_cxx_path)

    if(_clang15_cxx_path STREQUAL "")
      continue()
    endif()

    # Confirm it is 15.x
    set(${out_var} "${_path}" PARENT_SCOPE)
    return()
  endforeach()

  # If none of the candidates is found, set the output variable to empty
  set(${out_var} "" PARENT_SCOPE)
endfunction()

# Check the current C compiler
if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang"
  OR NOT CMAKE_C_COMPILER_VERSION MATCHES "^15\\.")
  message_green("Current C compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
  message_green("Need Clang 15, trying to find it automatically...")
  set(_clang15_path "")
  find_and_verify_clang15(_clang15_path)

  if(NOT "${_clang15_path}" STREQUAL "")
    derive_clangxx("${_clang15_path}" _clang15_cxx_path)
    set(CMAKE_C_COMPILER "${_clang15_path}" CACHE STRING "C compiler" FORCE)
    set(CMAKE_CXX_COMPILER "${_clang15_cxx_path}" CACHE STRING "C++ compiler" FORCE)
    message_green("Found Clang15: ${_clang15_path}, please switch to it")
    message_green("Found Clang++15: ${_clang15_cxx_path}, please switch to it")
    message(FATAL_ERROR
      "Please run the following CMake command to specify clang-15 as the compiler:
        cmake -DCMAKE_C_COMPILER=${_clang15_path} \\
              -DCMAKE_CXX_COMPILER=${_clang15_cxx_path} \\
              <other options>
or set CC/CXX environment variables before cmake configure:
        CC=${_clang15_path} \\
        CXX=${_clang15_cxx_path} \\
        cmake <other options>
    "
    )
  else()
    message(FATAL_ERROR
      "Cannot find Clang-15 compiler.\n"
      "Please specify Clang-15 via environment variables CC/CXX or CMake variables CMAKE_C_COMPILER/CMAKE_CXX_COMPILER\n"
    )
  endif()
endif()
