# Infuse-IoT C library tweaks

if(CONFIG_INFUSE_LIBC_FAST_MEM_FUNCTIONS)
  # Query the toolchain for the C library without -Os, which selects the
  # speed optimised multilib instead of the size optimised "space" variant.
  execute_process(
    COMMAND ${CMAKE_C_COMPILER} ${TOOLCHAIN_C_FLAGS} -print-file-name=libc.a
    OUTPUT_VARIABLE speed_libc
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  set(fast_mem_dir ${CMAKE_CURRENT_BINARY_DIR}/fast_mem)
  file(REMOVE_RECURSE ${fast_mem_dir})
  file(MAKE_DIRECTORY ${fast_mem_dir})

  # Depending on the multilib, each function is implemented in either the
  # assembly (.S.o) or C (.c.o) archive member, with the other member empty.
  # Extract both and keep whichever actually defines the function.
  set(fast_mem_paths)
  set(fast_mem_found TRUE)
  if(IS_ABSOLUTE "${speed_libc}" AND EXISTS "${speed_libc}")
    foreach(func memcpy memset)
      execute_process(
        COMMAND ${CMAKE_AR} x ${speed_libc} ${func}.S.o ${func}.c.o
        WORKING_DIRECTORY ${fast_mem_dir}
        OUTPUT_QUIET
        ERROR_QUIET
      )
      set(func_obj)
      foreach(obj ${fast_mem_dir}/${func}.S.o ${fast_mem_dir}/${func}.c.o)
        if(EXISTS ${obj})
          execute_process(
            COMMAND ${CMAKE_NM} --defined-only ${obj}
            OUTPUT_VARIABLE obj_symbols
            ERROR_QUIET
          )
          if(obj_symbols MATCHES "[ \t]T ${func}\n")
            set(func_obj ${obj})
            break()
          endif()
        endif()
      endforeach()
      if(func_obj)
        list(APPEND fast_mem_paths ${func_obj})
      else()
        set(fast_mem_found FALSE)
      endif()
    endforeach()
  else()
    set(fast_mem_found FALSE)
  endif()

  if(fast_mem_found)
    # Zephyr libraries are linked as whole archives, so these definitions
    # take precedence over the C library archive members.
    zephyr_library_named(infuse_libc_fast_mem)
    zephyr_library_sources(${fast_mem_paths})
    set_target_properties(infuse_libc_fast_mem PROPERTIES LINKER_LANGUAGE C)
  else()
    message(WARNING "Speed optimised memcpy/memset not found in '${speed_libc}', "
                    "using the default C library implementations")
  endif()
endif()
