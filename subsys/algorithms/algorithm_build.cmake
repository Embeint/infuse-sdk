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

    # Include Directories
    list(APPEND CFLAGS "-I${INFUSE_SDK_BASE}/include")
    list(APPEND CFLAGS "-I${ZEPHYR_BASE}/include")
    foreach(INC_FOLDER IN LISTS INC_FOLDERS)
      list(APPEND CFLAGS "-I${INC_FOLDER}")
    endforeach()

    # Build the algorithm for each supported floating-point mode
    foreach(FP_MODE IN LISTS PROFILE_FP_MODES)
      algorithm_target_name_core(${ALG_NAME} ${PROFILE_NAME} ${FP_MODE} target_name)
      algorithm_target_dir_core(${PROFILE_NAME} ${FP_MODE} ${OUTPUT_BASE} target_folder)
      make_directory(${target_folder})
      add_custom_command(
        OUTPUT
          ${target_folder}/${ALG_NAME}.llext.stripped
          ${target_folder}/${ALG_NAME}.llext
          ${target_folder}/${ALG_NAME}.inc
        # Compile to an object file
        COMMAND ${CMAKE_C_COMPILER} ${CFLAGS} "-mfloat-abi=${FP_MODE}"
          -o ${target_folder}/${ALG_NAME}.llext
          ${SRC_FILES}
        # Strip unneeded sections from the object file
        COMMAND ${CMAKE_OBJCOPY} --strip-unneeded
          --remove-section .comment
          --remove-section .ARM.attributes
          ${target_folder}/${ALG_NAME}.llext
          ${target_folder}/${ALG_NAME}.llext.stripped
        # Convert object file to a hex list that can be included into a C array
        COMMAND ${PYTHON_EXECUTABLE} ${ZEPHYR_BASE}/scripts/build/file2hex.py
          --file ${target_folder}/${ALG_NAME}.llext.stripped > ${target_folder}/${ALG_NAME}.inc
        DEPENDS
          ${SRC_FILES}
        COMMENT "Generating ${ALG_NAME} for configuration ${PROFILE_NAME}-${FP_MODE}"
      )
      add_custom_target(${target_name} ALL DEPENDS ${target_folder}/${ALG_NAME}.llext.stripped)
    endforeach()
  endforeach()
endfunction()
