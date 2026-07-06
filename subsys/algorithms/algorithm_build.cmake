# SPDX-License-Identifier: FSL-1.1-ALv2

file(GLOB PROFILE_FILES "${CMAKE_CURRENT_LIST_DIR}/profiles/profile_*.cmake")
set(ALGORITHM_BUILD_DIR "${CMAKE_CURRENT_LIST_DIR}")

# Determine the configuration name for any application
function(target_name_core
    CPU               # CPU type (cortex_m3, cortex_m4, etc)
    FP                # Floating point ABI (hard, soft, softfp)
    CONFIG_OUT        # Variable that the configuration name will be output in
    )
  set(${CONFIG_OUT} "${CPU}-${FP}" PARENT_SCOPE)
endfunction()

# Determine the target name for any application
function(algorithm_target_name_core
    ALG_NAME          # Name of the algorithm
    CPU               # CPU type (cortex_m3, cortex_m4, etc)
    FP                # Floating point ABI (hard, soft, softfp)
    TARGET_OUT        # Variable that the target name will be output in
    )
  set(${TARGET_OUT} "${ALG_NAME}-${CPU}-${FP}" PARENT_SCOPE)
endfunction()

# Determine the output directory for any application
function(algorithm_target_dir_core
    CPU               # CPU type (cortex_m3, cortex_m4, etc)
    FP                # Floating point ABI (hard, soft, softfp)
    OUTPUT_BASE       # Base folder that algorithms are built into
    DIRECTORY_OUT     # Folder the algorithm will be built in
    )
  set(${DIRECTORY_OUT} "${OUTPUT_BASE}/${CPU}/${FP}" PARENT_SCOPE)
endfunction()

# Determine the target name for the current application
function(algorithm_target_name
    ALG_NAME          # Name of the algorithm
    TARGET_OUT        # Variable that the target name will be output in
    )
  algorithm_target_name_core(
    ${ALG_NAME}
    ${CONFIG_INFUSE_ALGORITHM_CPU_NAME}
    ${CONFIG_INFUSE_ALGORITHM_FLOAT_ABI_DIR}
    target_out
  )
  set(${TARGET_OUT} ${target_out} PARENT_SCOPE)
endfunction()

# Determine the output folder for the current application
function(algorithm_target_dir
    OUTPUT_BASE       # Base folder that algorithms are built into
    DIRECTORY_OUT     # Folder the algorithm will be built in
    )
  algorithm_target_dir_core(
    ${CONFIG_INFUSE_ALGORITHM_CPU_NAME}
    ${CONFIG_INFUSE_ALGORITHM_FLOAT_ABI_DIR}
    ${OUTPUT_BASE}
    directory_out
  )
  set(${DIRECTORY_OUT} ${directory_out} PARENT_SCOPE)
endfunction()

# Determine the generated include file for the current application
function(algorithm_target_inc
    ALG_NAME          # Name of the algorithm
    OUTPUT_BASE       # Base folder that algorithms are built into
    FILE_OUT          # Generated include file path
    )
  algorithm_target_dir(${OUTPUT_BASE} directory_out)
  set(${FILE_OUT} "${directory_out}/${ALG_NAME}.inc" PARENT_SCOPE)
endfunction()

# Generate targets to build a given algorithm for all application configurations
function(algorithm_generate_targets
    ALG_NAME          # Name of the algorithm
    )

  cmake_parse_arguments(ARG
    ""
    "OUTPUT_BASE"
    "SOURCES;INCLUDES;TARGET_WHITELIST"
    ${ARGN}
  )

  if(NOT ARG_OUTPUT_BASE)
    message(FATAL_ERROR "algorithm_generate_targets requires OUTPUT_BASE")
  endif()
  if(NOT ARG_SOURCES)
    message(FATAL_ERROR "algorithm_generate_targets requires SOURCES")
  endif()

  foreach(PROFILE_FILE IN LISTS PROFILE_FILES)
    message(DEBUG "Profile file: ${PROFILE_FILE}")

    # Import the CFLAGS and variants from the profile
    include("${PROFILE_FILE}")

    # Build the algorithm for each supported floating-point mode
    foreach(FP_MODE IN LISTS PROFILE_FP_MODES)
      algorithm_target_name_core(${ALG_NAME} ${PROFILE_NAME} ${FP_MODE} target_name)
      algorithm_target_dir_core(${PROFILE_NAME} ${FP_MODE} ${ARG_OUTPUT_BASE} target_folder)
      # Check if this target is allowed
      if(ARG_TARGET_WHITELIST)
        target_name_core(${PROFILE_NAME} ${FP_MODE} target_cfg)
        list(FIND ARG_TARGET_WHITELIST ${target_cfg} target_index)
        if(target_index EQUAL -1)
          message(DEBUG "Skipping ${target_name} because
                  it is not in whitelist: ${ARG_TARGET_WHITELIST}")
          continue()
        endif()
      endif()
      set(llext_file ${target_folder}/${ALG_NAME}.llext)
      set(map_file ${target_folder}/${ALG_NAME}.map)
      set(stripped_file ${target_folder}/${ALG_NAME}.llext.stripped)
      set(inc_file ${target_folder}/${ALG_NAME}.inc)

      set(obj_target ${target_name}_objects)
      add_library(${obj_target} OBJECT ${ARG_SOURCES})
      target_compile_options(${obj_target} PRIVATE
        ${CFLAGS}
        "-mfloat-abi=${FP_MODE}"
      )
      target_include_directories(${obj_target} PRIVATE
        ${INFUSE_SDK_BASE}/include
        ${INFUSE_SDK_BASE}/generated/include
        ${ZEPHYR_BASE}/include
        ${ARG_INCLUDES}
      )

      # Link all object files into a single relocatable object
      add_custom_command(
        OUTPUT
          ${stripped_file}
          ${llext_file}
          ${inc_file}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${target_folder}
        COMMAND ${CMAKE_LINKER} -r $<TARGET_OBJECTS:${obj_target}> -o ${llext_file} -Map=${map_file}
        # Strip unneeded sections from the object file
        COMMAND ${CMAKE_OBJCOPY} --strip-unneeded
          --remove-section .comment
          --remove-section .ARM.attributes
          ${llext_file}
          ${stripped_file}
        # Convert object file to a hex list that can be included into a C array
        COMMAND ${CMAKE_COMMAND}
          -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
          -DFILE2HEX_SCRIPT=${ZEPHYR_BASE}/scripts/build/file2hex.py
          -DINPUT_FILE=${stripped_file}
          -DOUTPUT_FILE=${inc_file}
          -P ${ALGORITHM_BUILD_DIR}/file2hex.cmake
        DEPENDS
          $<TARGET_OBJECTS:${obj_target}>
          ${ALGORITHM_BUILD_DIR}/file2hex.cmake
        BYPRODUCTS
          ${map_file}
        COMMAND_EXPAND_LISTS
        VERBATIM
        COMMENT "Generating ${ALG_NAME} for configuration ${PROFILE_NAME}-${FP_MODE}"
      )
      add_custom_target(${target_name} ALL DEPENDS ${inc_file})
    endforeach()
  endforeach()
endfunction()
