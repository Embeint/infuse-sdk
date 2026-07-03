# SPDX-License-Identifier: FSL-1.1-ALv2

file(GLOB PROFILE_FILES "${CMAKE_CURRENT_LIST_DIR}/profiles/profile_*.cmake")
set(ALGORITHM_BUILD_DIR "${CMAKE_CURRENT_LIST_DIR}")

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

# Determine the nRF Edge AI library for a CPU profile
function(nrf_edgeai_lib_path
    CPU               # CPU type (cortex_m3, cortex_m4, etc)
    LIBRARY_OUT       # Variable that the library path will be output in
    )
  string(REPLACE "_" "-" nrf_edgeai_cpu_dir ${CPU})
  set(nrf_edgeai_lib
    ${ZEPHYR_SDK_EDGE_AI_MODULE_DIR}/lib/nrf_edgeai/${nrf_edgeai_cpu_dir}/libnrf_edgeai_${nrf_edgeai_cpu_dir}.a
  )
  if(EXISTS ${nrf_edgeai_lib})
    set(${LIBRARY_OUT} ${nrf_edgeai_lib} PARENT_SCOPE)
  else()
    set(${LIBRARY_OUT} PARENT_SCOPE)
  endif()
endfunction()

# Generate targets to build a given algorithm for all application configurations
function(algorithm_generate_targets
    ALG_NAME          # Name of the algorithm
    )

  cmake_parse_arguments(ARG
    "NRF_EDGEAI"
    "OUTPUT_BASE"
    "SOURCES;INCLUDES"
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
      if(ARG_NRF_EDGEAI)
        if(NOT "${FP_MODE}" STREQUAL "hard")
          message(DEBUG "Skipping ${PROFILE_NAME}-${FP_MODE}: nRF Edge AI requires hard-float ABI")
          continue()
        endif()
        nrf_edgeai_lib_path(${PROFILE_NAME} nrf_edgeai_lib)
        if(NOT nrf_edgeai_lib)
          message(DEBUG "Skipping ${PROFILE_NAME}: nRF Edge AI library is not available")
          continue()
        endif()
      endif()

      algorithm_target_name_core(${ALG_NAME} ${PROFILE_NAME} ${FP_MODE} target_name)
      algorithm_target_dir_core(${PROFILE_NAME} ${FP_MODE} ${ARG_OUTPUT_BASE} target_folder)
      set(llext_file ${target_folder}/${ALG_NAME}.llext)
      set(map_file ${target_folder}/${ALG_NAME}.map)
      set(stripped_file ${target_folder}/${ALG_NAME}.llext.stripped)
      set(inc_file ${target_folder}/${ALG_NAME}.inc)
      set(exported_sym_link_args ${target_folder}/${ALG_NAME}.exported_sym.args)

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

      set(target_link_files)
      if(ARG_NRF_EDGEAI)
        target_include_directories(${obj_target} PRIVATE
          ${ZEPHYR_SDK_EDGE_AI_MODULE_DIR}/include
        )
        set(target_link_files ${nrf_edgeai_lib})
      endif()

      # Link all object files into a single relocatable object
      add_custom_command(
        OUTPUT
          ${exported_sym_link_args}
          ${stripped_file}
          ${llext_file}
          ${inc_file}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${target_folder}
        COMMAND ${CMAKE_COMMAND}
          -DREADELF=${CMAKE_READELF}
          -DOUTPUT_FILE=${exported_sym_link_args}
          -DOBJECT_FILES=$<JOIN:$<TARGET_OBJECTS:${obj_target}>,|>
          -P ${ALGORITHM_BUILD_DIR}/exported_sym_link_args.cmake
        COMMAND ${CMAKE_LINKER} -r
          -T ${ALGORITHM_BUILD_DIR}/llext_sections.ld
          $<TARGET_OBJECTS:${obj_target}>
          @${exported_sym_link_args} --gc-sections
          ${target_link_files}
          -Map=${map_file}
          -o ${llext_file}
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
          ${target_link_files}
          ${ALGORITHM_BUILD_DIR}/file2hex.cmake
          ${ALGORITHM_BUILD_DIR}/exported_sym_link_args.cmake
          ${ALGORITHM_BUILD_DIR}/llext_sections.ld
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
