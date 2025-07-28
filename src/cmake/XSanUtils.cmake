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

# Attach a POST_BUILD command to a target to create a “directory to directory” symbolic link.
function(add_post_build_relative_directory_symlink)
  set(options "")
  set(oneValueArgs "TRIGGER_TARGET" "SOURCE_DIR" "LINK_PATH")
  set(multiValueArgs "")

  cmake_parse_arguments(ARGS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT ARGS_TRIGGER_TARGET)
    message(FATAL_ERROR "add_post_build_relative_directory_symlink: Missing required argument: TRIGGER_TARGET")
  endif()
  if(NOT ARGS_SOURCE_DIR)
    message(FATAL_ERROR "add_post_build_relative_directory_symlink: Missing required argument: SOURCE_DIR")
  endif()
  if(NOT ARGS_LINK_PATH)
    message(FATAL_ERROR "add_post_build_relative_directory_symlink: Missing required argument: LINK_PATH")
  endif()

  get_filename_component(LINK_DIR "${ARGS_LINK_PATH}" DIRECTORY)
  get_filename_component(LINK_NAME "${ARGS_LINK_PATH}" NAME)

  file(RELATIVE_PATH RELATIVE_SOURCE_PATH "${LINK_DIR}" "${ARGS_SOURCE_DIR}")

  add_custom_command(
      TARGET ${ARGS_TRIGGER_TARGET}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${LINK_DIR}"
      COMMAND ${CMAKE_COMMAND} -E create_symlink
              "${RELATIVE_SOURCE_PATH}"
              "${LINK_NAME}"
      WORKING_DIRECTORY "${LINK_DIR}"
      COMMENT "Creating relative directory symlink: ${ARGS_LINK_PATH} -> ${RELATIVE_SOURCE_PATH}"
  )
endfunction()

# Attach a POST_BUILD command to a target to link files from another target to a specified directory.
function(add_post_build_relative_target_symlink)
  set(options "")
  set(oneValueArgs "TRIGGER_TARGET" "SOURCE_TARGET" "DESTINATION_DIR")
  set(multiValueArgs "")

  cmake_parse_arguments(ARGS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT ARGS_TRIGGER_TARGET)
    message(FATAL_ERROR "add_post_build_relative_target_symlink: Missing required argument: TRIGGER_TARGET")
  endif()
  if(NOT ARGS_SOURCE_TARGET)
    message(FATAL_ERROR "add_post_build_relative_target_symlink: Missing required argument: SOURCE_TARGET")
  endif()
  if(NOT ARGS_DESTINATION_DIR)
    message(FATAL_ERROR "add_post_build_relative_target_symlink: Missing required argument: DESTINATION_DIR")
  endif()

  # Get the actual output directory of the source target at configure time
  get_target_property(source_output_dir ${ARGS_SOURCE_TARGET} ARCHIVE_OUTPUT_DIRECTORY)
  if(NOT source_output_dir)
    get_target_property(source_output_dir ${ARGS_SOURCE_TARGET} LIBRARY_OUTPUT_DIRECTORY)
  endif()
  if(NOT source_output_dir)
    get_target_property(source_output_dir ${ARGS_SOURCE_TARGET} RUNTIME_OUTPUT_DIRECTORY)
  endif()
  if(NOT source_output_dir)
    set(source_output_dir ${CMAKE_CURRENT_BINARY_DIR})
  endif()

  # Calculate relative path using actual directories
  file(RELATIVE_PATH relative_target_path
    "${ARGS_DESTINATION_DIR}"
    "${source_output_dir}"
  )

  add_custom_command(
      TARGET ${ARGS_TRIGGER_TARGET}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${ARGS_DESTINATION_DIR}"
      COMMAND ${CMAKE_COMMAND} -E create_symlink
              "${relative_target_path}/$<TARGET_FILE_NAME:${ARGS_SOURCE_TARGET}>"
              "$<TARGET_FILE_NAME:${ARGS_SOURCE_TARGET}>"
      WORKING_DIRECTORY "${ARGS_DESTINATION_DIR}"
      COMMENT "Creating relative symlink for target ${ARGS_SOURCE_TARGET} in ${ARGS_DESTINATION_DIR}/"
  )
endfunction()
