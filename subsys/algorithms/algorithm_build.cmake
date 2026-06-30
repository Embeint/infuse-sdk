# SPDX-License-Identifier: FSL-1.1-ALv2

file(GLOB PROFILE_FILES "${CMAKE_CURRENT_LIST_DIR}/profiles/profile_*.cmake")

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

# Generate targets to build a given algorithm for all application configurations
function(algorithm_generate_targets
    ALG_NAME          # Name of the algorithm
    SRC_FILES         # Source files that implement the algorithm
    INC_FOLDERS       # Extra include folders for the algorithm
    OUTPUT_BASE       # Base folder that algortithms are built into
    )

  foreach(PROFILE_FILE IN LISTS PROFILE_FILES)
    message(DEBUG "Profile file: ${PROFILE_FILE}")

    # Import the CFLAGS and variants from the profile
    include("${PROFILE_FILE}")

    # Build the algorithm for each supported floating-point mode
    foreach(FP_MODE IN LISTS PROFILE_FP_MODES)
      algorithm_target_name_core(${ALG_NAME} ${PROFILE_NAME} ${FP_MODE} target_name)
      algorithm_target_dir_core(${PROFILE_NAME} ${FP_MODE} ${OUTPUT_BASE} target_folder)
      set(llext_file ${target_folder}/${ALG_NAME}.llext)
      set(stripped_file ${target_folder}/${ALG_NAME}.llext.stripped)
      set(inc_file ${target_folder}/${ALG_NAME}.inc)

      set(obj_target ${target_name}_objects)
      add_library(${obj_target} OBJECT ${SRC_FILES})
      target_compile_options(${obj_target} PRIVATE
        ${CFLAGS}
        "-mfloat-abi=${FP_MODE}"
      )
      target_include_directories(${obj_target} PRIVATE
        ${INFUSE_SDK_BASE}/include
        ${INFUSE_SDK_BASE}/generated/include
        ${ZEPHYR_BASE}/include
        ${INC_FOLDERS}
      )

      # Link all object files into a single relocatable object
      add_custom_command(
        OUTPUT
          ${stripped_file}
          ${llext_file}
          ${inc_file}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${target_folder}
        COMMAND ${CMAKE_LINKER} -r $<TARGET_OBJECTS:${obj_target}> -o ${llext_file}
        # Strip unneeded sections from the object file
        COMMAND ${CMAKE_OBJCOPY} --strip-unneeded
          --remove-section .comment
          --remove-section .ARM.attributes
          ${llext_file}
          ${stripped_file}
        # Convert object file to a hex list that can be included into a C array
        COMMAND ${PYTHON_EXECUTABLE} ${ZEPHYR_BASE}/scripts/build/file2hex.py
          --file ${stripped_file} > ${inc_file}
        DEPENDS
          ${obj_target}
        COMMAND_EXPAND_LISTS
        COMMENT "Generating ${ALG_NAME} for configuration ${PROFILE_NAME}-${FP_MODE}"
      )
      add_custom_target(${target_name} ALL DEPENDS ${stripped_file})
    endforeach()
  endforeach()
endfunction()
