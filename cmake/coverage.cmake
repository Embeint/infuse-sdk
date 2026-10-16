# Coverage testing helpers

if(CONFIG_COVERAGE_INFUSE_SDK)
  function(infuse_enable_sdk_coverage)
    zephyr_get_targets(
      "${CMAKE_SOURCE_DIR}"
      "EXECUTABLE;MODULE_LIBRARY;OBJECT_LIBRARY;SHARED_LIBRARY;STATIC_LIBRARY"
      build_targets
    )

    set(infuse_source_dir "${INFUSE_DIR}")
    foreach(build_target ${build_targets})
      get_target_property(target_source_dir ${build_target} SOURCE_DIR)
      cmake_path(IS_PREFIX infuse_source_dir "${target_source_dir}"
        NORMALIZE is_infuse_target
      )

      if(NOT is_infuse_target)
        continue()
      endif()

      # Loadable extensions cannot resolve the gcov runtime symbols from the
      # base image, so leave extension objects uninstrumented.
      get_target_property(target_compile_definitions ${build_target}
        COMPILE_DEFINITIONS
      )
      get_target_property(target_compile_options ${build_target}
        COMPILE_OPTIONS
      )
      if("LL_EXTENSION_BUILD" IN_LIST target_compile_definitions OR
         "-DLL_EXTENSION_BUILD" IN_LIST target_compile_options)
        continue()
      endif()

      target_compile_options(${build_target} PRIVATE
        $<TARGET_PROPERTY:compiler,coverage>
      )
    endforeach()
  endfunction()

  # Applications are added after Zephyr modules have been processed. Defer
  # target discovery until the top-level source directory is complete so both
  # the SDK libraries and test application targets are included.
  cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
    CALL infuse_enable_sdk_coverage
  )
endif()
