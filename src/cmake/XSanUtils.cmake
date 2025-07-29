function(message_green)
  string(ASCII 27 ESC)
  set(GREEN "${ESC}[32m")
  set(RESET "${ESC}[0m")
  message(STATUS "${GREEN}${ARGV}${RESET}")
endfunction()


function(safe_relative_path out_var base_dir target_dir)
  file(RELATIVE_PATH _rel_path "${base_dir}" "${target_dir}")
  if (_rel_path STREQUAL "")
      set(${out_var} "." PARENT_SCOPE)
  else()
      set(${out_var} "${_rel_path}" PARENT_SCOPE)
  endif()
endfunction()

macro(extend_list_if condition value)
  if(${condition})
    foreach(list ${ARGN})
      list(APPEND ${list} ${${value}})
    endforeach()
  endif()
endmacro()

#Because `append_string_if` generates an extra space before the newly added string
macro(extend_string_if condition value)
  if(${condition})
    foreach(str ${ARGN})
      set(${str} "${${str}}${value}")
    endforeach()
  endif()
endmacro()

function(transform_list_in_place list_name)
  list(TRANSFORM ${list_name} ${ARGN} OUTPUT_VARIABLE _transformed)
  set(${list_name} "${_transformed}" PARENT_SCOPE)
endfunction()

# This function generates all power sets of a given list and sets them as variables in the parent scope.
function(list_powerset input_list output_var_list_of_names)
  set(local_list_of_generated_subset_var_names "")
  set(sorted_internal_input_list ${input_list})
  if(sorted_internal_input_list)
    list(SORT sorted_internal_input_list)
  endif()
  list(LENGTH sorted_internal_input_list N)
  math(EXPR num_subsets "1 << ${N}")
  math(EXPR num_subsets_minus_1 "${num_subsets} - 1")
  if(${N} EQUAL 0)
    set(subset_variable_name "XSAN_SUBSET_EMPTY")
    set(${subset_variable_name} "" PARENT_SCOPE)
    list(APPEND local_list_of_generated_subset_var_names "${subset_variable_name}")
    set(${output_var_list_of_names} "${local_list_of_generated_subset_var_names}" PARENT_SCOPE)
    return()
  endif()
  math(EXPR N_minus_1 "${N} - 1")
  foreach(i RANGE 0 ${num_subsets_minus_1})
    set(current_subset_elements_list "")
    foreach(j RANGE 0 ${N_minus_1})
      math(EXPR j_th_bit_is_set "((${i} >> ${j}) & 1)")
      if(j_th_bit_is_set)
        list(GET sorted_internal_input_list ${j} element_to_add)
        list(APPEND current_subset_elements_list "${element_to_add}")
      endif()
    endforeach()
    list(PREPEND current_subset_elements_list "Asan")
    list(SORT current_subset_elements_list)
    list(JOIN current_subset_elements_list "_" subset_variable_name_suffix_joined)
    string(TOUPPER "${subset_variable_name_suffix_joined}" subset_variable_name_suffix_uppercase)
    set(subset_variable_name "XSAN_SUBSET_${subset_variable_name_suffix_uppercase}")

    set(${subset_variable_name} "${current_subset_elements_list}" PARENT_SCOPE)
    list(APPEND local_list_of_generated_subset_var_names "${subset_variable_name}")
  endforeach()
  set(${output_var_list_of_names} "${local_list_of_generated_subset_var_names}" PARENT_SCOPE)
endfunction()

# Generate a complete set of XSAN_CONTAINS_* macro definitions based on the given combination of sanitizer
function(create_xsan_combination_definitions combination_variable_name universe_list output_variable_name)
  set(generated_definitions "")
  set(current_combination_sans ${${combination_variable_name}})
  set(all_possible_sans ${universe_list})
  list(APPEND all_possible_sans "Asan")
  list(REMOVE_DUPLICATES all_possible_sans)
  foreach(san_to_check IN LISTS all_possible_sans)
    string(TOUPPER "${san_to_check}" SAN_TO_CHECK_UPPER)
    list(FIND current_combination_sans "${san_to_check}" is_present_index)

    if(is_present_index GREATER -1)
      list(APPEND generated_definitions "XSAN_CONTAINS_${SAN_TO_CHECK_UPPER}=1")
    else()
      list(APPEND generated_definitions "XSAN_CONTAINS_${SAN_TO_CHECK_UPPER}=0")
    endif()
  endforeach()
  set(${output_variable_name} "${generated_definitions}" PARENT_SCOPE)
endfunction()

# Dump the output directory of a target to ${out_var}
function(get_target_output_directory target out_var)
  # Try to resolve the output directory of the target from the following properties:
  # ARCHIVE_OUTPUT_DIRECTORY, LIBRARY_OUTPUT_DIRECTORY, RUNTIME_OUTPUT_DIRECTORY
  foreach(prop_type ARCHIVE LIBRARY RUNTIME)
    get_target_property(dir_${prop_type} ${target} "${prop_type}_OUTPUT_DIRECTORY")

    if(dir_${prop_type})
      break()
    endif()
  endforeach()

  # If no configuration-specific directory is found, fall back to the generic directory
  if(NOT dir_ARCHIVE AND NOT dir_LIBRARY AND NOT dir_RUNTIME)
    foreach(prop_type ARCHIVE LIBRARY RUNTIME)
      get_target_property(dir_${prop_type} ${target} "${prop_type}_OUTPUT_DIRECTORY")

      if(dir_${prop_type})
        break()
      endif()
    endforeach()
  endif()

  # Determine the final directory (priority: RUNTIME > LIBRARY > ARCHIVE)
  if(dir_RUNTIME)
    set(output_dir "${dir_RUNTIME}")
  elseif(dir_LIBRARY)
    set(output_dir "${dir_LIBRARY}")
  elseif(dir_ARCHIVE)
    set(output_dir "${dir_ARCHIVE}")
  else()
    # If all directories are not set, use the default output directory
    get_target_property(output_dir ${target} "RUNTIME_OUTPUT_DIRECTORY")

    if(NOT output_dir)
      set(output_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

      if(NOT output_dir)
        set(output_dir "${CMAKE_CURRENT_BINARY_DIR}")
      endif()
    endif()
  endif()

  # Return the result
  set(${out_var} "${output_dir}" PARENT_SCOPE)
endfunction()

# ==============================================================================
# function: install_symlink
#
# Creates a symbolic link during the installation phase.
#
# Usage:
#   install_symlink(
#     LINK_NAME <link_name>
#     TARGET_PATH <target_path>
#     WORK_DIR <work_dir>
#   )
#  
# LINK_NAME    : The name of the symbolic link to create.
# TARGET_PATH  : The path the symlink should point to (can be relative).
# WORK_DIR     : The working directory (relative to CMAKE_INSTALL_PREFIX).
#
# I.e., cd ${WORK_DIR}; ln -s ${TARGET_PATH} ${LINK_NAME}
function(install_symlink)
  set(options "")
  set(oneValueArgs LINK_NAME TARGET_PATH WORK_DIR COMPONENT)
  set(multiValueArgs "")
  cmake_parse_arguments(ARGS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT ARGS_LINK_NAME)
    message(FATAL_ERROR "install_symlink: lack parameter LINK_NAME")
  endif()
  if(NOT ARGS_TARGET_PATH)
    message(FATAL_ERROR "install_symlink: lack parameter TARGET_PATH")
  endif()
  if(NOT ARGS_WORK_DIR)
    message(FATAL_ERROR "install_symlink: lack parameter DESTINATION")
  endif()

  # Install lib/*/xsan_* symlink
  ## Install Directory does not support SymLink
  # install(
  #   DIRECTORY "${symlibdir_${libname}}/"
  #   DESTINATION "${install_dir}"
  #   USE_SOURCE_PERMISSIONS
  #   COMPONENT xsan
  # )
  cmake_path(SET TARGET_PATH NORMALIZE "${ARGS_TARGET_PATH}")

  install(CODE
    "
    # Construct the true destination path by prepending DESTDIR
    # Cannot only use CMAKE_INSTALL_PREFIX to support CPack
    set(FULL_WORK_DIR \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${ARGS_WORK_DIR}\")

    message(STATUS \"Installing symlink: ${ARGS_WORK_DIR}/${ARGS_LINK_NAME} -> ${TARGET_PATH}\")
    message(STATUS \"full_dest_dir: \${FULL_WORK_DIR}\")
    if(NOT EXISTS \"\${FULL_WORK_DIR}\")
      file(MAKE_DIRECTORY \"\${FULL_WORK_DIR}\")
    endif()
    execute_process(
      COMMAND \${CMAKE_COMMAND} -E create_symlink
        \"${TARGET_PATH}\"
        \"${ARGS_LINK_NAME}\"
      WORKING_DIRECTORY \"\${FULL_WORK_DIR}\"
    )
    "
    COMPONENT ${ARGS_COMPONENT}
  )
endfunction()
# ==============================================================================



# Build a symlink in output/install dir, i.e., 
# Create symlink in output dir:
#    cd ${outdir}; 
#    ln -s ${SOURCE} ${LINK_NAME}
# Create symlink in install dir:
#    cd ${install_dir}; 
#    ln -s ${SOURCE} ${LINK_NAME}
function(add_symlink)
  set(options "")
  set(oneValueArgs "TRIGGER_TARGET" "SOURCE" "LINK_NAME" "OUT_DIR" "INSTALL_DIR")
  set(multiValueArgs "")

  cmake_parse_arguments(ARGS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT ARGS_SOURCE)
    message(FATAL_ERROR "add_symlink: Missing required argument: SOURCE")
  endif()
  if(NOT ARGS_LINK_NAME)
    message(FATAL_ERROR "add_symlink: Missing required argument: LINK_PATH")
  endif()
  if(NOT ARGS_OUT_DIR)
    message(FATAL_ERROR "add_symlink: Missing required argument: OUT_DIR")
  endif()
  if(NOT ARGS_INSTALL_DIR)
    message(FATAL_ERROR "add_symlink: Missing required argument: INSTALL_DIR")
  endif()

  cmake_path(SET SOURCE_PATH NORMALIZE "${ARGS_SOURCE}")

  if(ARGS_TRIGGER_TARGET)
    add_custom_command(
        TARGET ${ARGS_TRIGGER_TARGET}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
                "${SOURCE_PATH}"
                "${ARGS_LINK_NAME}"
        WORKING_DIRECTORY "${ARGS_OUT_DIR}"
        COMMENT "Creating symlink: ${ARGS_LINK_NAME} -> ${ARGS_SOURCE}"
        # When output change, run this command automatically 
        BYPRODUCTS ${ARGS_OUT_DIR}/${ARGS_LINK_NAME}
    )
    # We can also use this add_custom_target
    # add_custom_target(${ARGS_LINK_NAME} ALL
    #   COMMAND ${CMAKE_COMMAND} -E create_symlink
    #           "${ARGS_SOURCE}"
    #           "${ARGS_LINK_NAME}"
    #   WORKING_DIRECTORY "${ARGS_OUT_DIR}"
    #   COMMENT "Creating symlink: ${ARGS_LINK_NAME} -> ${ARGS_SOURCE}"
    #   BYPRODUCTS ${ARGS_OUT_DIR}/${ARGS_LINK_NAME}
    # )
    # add_dependencies(${ARGS_TRIGGER_TARGET} ${ARGS_LINK_NAME})
  else()
    add_custom_command(
      COMMAND ${CMAKE_COMMAND} -E create_symlink
              "${SOURCE_PATH}"
              "${ARGS_LINK_NAME}"
      WORKING_DIRECTORY "${ARGS_OUT_DIR}"
      COMMENT "Creating symlink: ${ARGS_LINK_NAME} -> ${ARGS_SOURCE}"
      BYPRODUCTS ${ARGS_OUT_DIR}/${ARGS_LINK_NAME}
    )
  endif()

  install_symlink(
    LINK_NAME "${ARGS_LINK_NAME}"
    TARGET_PATH "${SOURCE_PATH}"
    WORK_DIR "${ARGS_INSTALL_DIR}"
  )
endfunction()


# Add symlink to the target, i.e., 
# Create symlink in output dir:
#    cd ${target_outdir}; 
#    rel_path=$(realpath --relative-to=${REL_DEST_DIR} ${target_file})
#    ln -s ${rel_path} ${REL_DEST_DIR}/
# Create symlink in install dir:
#    cd ${target_install_dir}; 
#    rel_path=$(realpath --relative-to=${REL_DEST_DIR} ${target_install_file})
#    ln -s ${rel_path} ${REL_DEST_DIR}/
function(add_target_symlink)
  set(options "")
  set(oneValueArgs "SOURCE_TARGET" "REL_DEST_DIR" "INSTALL_DIR")
  set(multiValueArgs "")

  cmake_parse_arguments(ARGS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT ARGS_SOURCE_TARGET)
    message(FATAL_ERROR "add_target_symlink: Missing required argument: SOURCE_TARGET")
  endif()
  if(NOT ARGS_REL_DEST_DIR)
    message(FATAL_ERROR "add_target_symlink: Missing required argument: REL_DEST_DIR")
  endif()
  if(NOT ARGS_INSTALL_DIR)
    message(FATAL_ERROR "add_target_symlink: Missing required argument: REL_INSTALL_DIR")
  endif()
  
  # Calculate relative path using actual directories
  get_target_output_directory(${ARGS_SOURCE_TARGET} target_output_dir)
  file(RELATIVE_PATH REL_SOURCE_DIR "${target_output_dir}/${ARGS_REL_DEST_DIR}" "${target_output_dir}")

  cmake_path(SET REL_SYMLINK_FILE_PATH
             NORMALIZE "${ARGS_REL_DEST_DIR}/$<TARGET_FILE_NAME:${ARGS_SOURCE_TARGET}>")
  cmake_path(SET REL_SOURCE_FILE_PATH
             NORMALIZE "${REL_SOURCE_DIR}/$<TARGET_FILE_NAME:${ARGS_SOURCE_TARGET}>")

  add_custom_command(
    TARGET ${ARGS_SOURCE_TARGET}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ARGS_REL_DEST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${REL_SOURCE_FILE_PATH}"
            "${REL_SYMLINK_FILE_PATH}"
    WORKING_DIRECTORY "${target_output_dir}"
    COMMENT 
    "Creating symlink for target ${ARGS_SOURCE_TARGET}, i.e.,
            ${REL_SYMLINK_FILE_PATH}
              -> 
            ${REL_SOURCE_FILE_PATH}
    "
    # Cannot use BYPRODUCTS here, because it advances build timing, making the target invisible
    # BYPRODUCTS "${target_output_dir}/${ARGS_REL_DEST_DIR}/$<TARGET_FILE_NAME:${ARGS_SOURCE_TARGET}>"
  )

  # DESTINATION: give a relative dir to support both install and CPack
  install(FILES
    "${target_output_dir}/${ARGS_REL_DEST_DIR}/$<TARGET_FILE_NAME:${ARGS_SOURCE_TARGET}>"
    DESTINATION "${ARGS_INSTALL_DIR}/${ARGS_REL_DEST_DIR}"
    COMPONENT xsan
  )
endfunction()
