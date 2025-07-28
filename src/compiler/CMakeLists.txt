include_directories(include)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
configure_file(config_compile.h.in config_compile.h)

add_custom_target(compiler)

# ################ For clang_wrapper.c #################
add_executable(xclang clang_wrapper.c)
add_dependencies(compiler xclang)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  # Suppress warning: `u8* to char*`, which is common in clang_wrapper.c
  target_compile_options(xclang PRIVATE -Wno-pointer-sign)
  target_compile_definitions(xclang PRIVATE ${XSAN_DEFINITIONS_TO_ADD})
endif()

# Set output directory
set_target_properties(xclang
  PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${XSAN_BIN_DIR}"
)

add_custom_command(TARGET xclang POST_BUILD
  COMMAND ln -sf "xclang" "${XSAN_BIN_DIR}/xclang++")

# ################ For gcc_wrapper.c  #################
add_executable(xgcc gcc_wrapper.c)
add_dependencies(compiler xgcc)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  # Suppress warning: `u8* to char*`, which is common in gcc_wrapper.c
  target_compile_options(xgcc PRIVATE -Wno-pointer-sign)
  target_compile_definitions(xgcc PRIVATE ${XSAN_DEFINITIONS_TO_ADD})
endif()

set_target_properties(xgcc
  PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${XSAN_BIN_DIR}"
)

add_custom_command(TARGET xgcc POST_BUILD
  COMMAND ln -sf "xgcc" "${XSAN_BIN_DIR}/xg++")

# ################ For unsupported sanitizers in gcc #################
set(GCC_UNSUPPORTED_SANITIZERS ${XSAN_SANITIZERS})
list(REMOVE_ITEM GCC_UNSUPPORTED_SANITIZERS ${GCC_SUPPORT_SANITIZERS})

foreach(unsupported_san IN LISTS GCC_UNSUPPORTED_SANITIZERS)
  target_compile_definitions(xgcc PRIVATE "COMPILER_UNSUPPORT_${unsupported_san}=1")
endforeach()

# ################ For unsupported sanitizers in clang #################
set(CLANG_UNSUPPORTED_SANITIZERS ${XSAN_SANITIZERS})
list(REMOVE_ITEM CLANG_UNSUPPORTED_SANITIZERS ${CLANG_SUPPORT_SANITIZERS})

foreach(unsupported_san IN LISTS CLANG_UNSUPPORTED_SANITIZERS)
  target_compile_definitions(xclang PRIVATE "COMPILER_UNSUPPORT_${unsupported_san}=1")
endforeach()

# ################ For install #################

# --- Step 1: put executable files to ${XSAN_INSTALL_BINDIR} ---
# Install from targets
install(TARGETS xclang xgcc
  RUNTIME # files required by runtime, i.e., executable/DLL
  DESTINATION ${XSAN_INSTALL_BINDIR}
  COMPONENT compiler
)

# Install from files
install(FILES "${XSAN_BIN_DIR}/xclang++" "${XSAN_BIN_DIR}/xg++"
  DESTINATION ${XSAN_INSTALL_BINDIR}
  COMPONENT compiler
)

# --- Step 2: create symlink from ${CMAKE_INSTALL_BINDIR} to ${XSAN_INSTALL_BINDIR} ---
# Create symlink, e.g., bin/xclang -> ../lib/xsan/xclang
# Define a list of all executable names that need symlinks
set(XSAN_EXECUTABLES xclang xgcc xclang++ xg++)

# Create the install directory for symlinks
install(CODE "
  file(MAKE_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
")

foreach(EXE_NAME IN LISTS XSAN_EXECUTABLES)
  install(CODE "
      # 1. Define the link directory, target file absolute path, and link file absolute path at install time
      set(LINK_DIR \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
      set(TARGET_FILE \"\${CMAKE_INSTALL_PREFIX}/${XSAN_INSTALL_DIR}/${XSAN_INSTALL_BINDIR}/${EXE_NAME}\")
      set(LINK_NAME \"\${LINK_DIR}/${EXE_NAME}\")

      # 2. Compute the relative path from the link directory to the target file
      file(RELATIVE_PATH RELATIVE_LINK_TARGET \"\${LINK_DIR}\" \"\${TARGET_FILE}\")

      # Print debug info to confirm the computed relative path
      message(STATUS \"Creating relative symlink: \${LINK_NAME} -> \${RELATIVE_LINK_TARGET}\")

      # 3. Create the symlink using the computed relative path
      if(EXISTS \${TARGET_FILE} AND NOT EXISTS \${LINK_NAME})
          execute_process(
              COMMAND \${CMAKE_COMMAND} -E create_symlink
                      \${RELATIVE_LINK_TARGET} # <to>   : relative path
                      \${LINK_NAME}            # <from> : full path
              RESULT_VARIABLE SYMLINK_RESULT
          )
          if(NOT SYMLINK_RESULT EQUAL 0)
              message(FATAL_ERROR \"Failed to create symlink \${LINK_NAME}\")
          endif()
      endif()
  "
    COMPONENT compiler
  )
endforeach()

# ################ For HotPatch #################
add_subdirectory(livepatch)
