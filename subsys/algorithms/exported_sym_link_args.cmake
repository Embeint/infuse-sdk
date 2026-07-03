# SPDX-License-Identifier: FSL-1.1-ALv2
#
# Parse object files to find all relocatable symbols in the
# .exported_sym group and save them into a file that can
# be used with the linker to preserve them when using --gc-sections.

if(NOT DEFINED READELF)
  message(FATAL_ERROR "READELF is required")
endif()
if(NOT DEFINED OUTPUT_FILE)
  message(FATAL_ERROR "OUTPUT_FILE is required")
endif()

# Convert the object list back from a generator-expression friendly separator.
set(link_args)
string(REPLACE "|" ";" object_files "${OBJECT_FILES}")

foreach(object_file IN LISTS object_files)
  # Read relocation records so references from .exported_sym can be used as GC roots.
  execute_process(
    COMMAND ${READELF} -r ${object_file}
    RESULT_VARIABLE readelf_result
    OUTPUT_VARIABLE relocations
    ERROR_VARIABLE readelf_error
  )
  if(NOT readelf_result EQUAL 0)
    message(FATAL_ERROR
      "Failed to read relocations from ${object_file}: ${readelf_error}"
    )
  endif()

  # Walk only the relocation section attached to .exported_sym.
  set(in_exported_sym_section FALSE)
  string(REPLACE "\n" ";" relocation_lines "${relocations}")
  foreach(line IN LISTS relocation_lines)
    if(line MATCHES "^Relocation section '\\.rel(a)?\\.exported_sym'")
      set(in_exported_sym_section TRUE)
      continue()
    endif()

    if(in_exported_sym_section AND line MATCHES "^Relocation section ")
      set(in_exported_sym_section FALSE)
    endif()

    if(in_exported_sym_section AND line MATCHES "^[0-9a-fA-F]+[ \t]+")
      # GNU and LLVM readelf put the referenced symbol in the fifth column.
      string(REGEX MATCHALL "[^ \t]+" fields "${line}")
      list(LENGTH fields field_count)
      if(field_count LESS 5)
        continue()
      endif()
      list(GET fields 4 symbol)
      if(NOT symbol MATCHES "^\\.")
        list(APPEND link_args -u ${symbol})
      endif()
    endif()
  endforeach()
endforeach()

# Emit a linker response file containing one -u argument per exported symbol.
list(REMOVE_DUPLICATES link_args)
file(WRITE ${OUTPUT_FILE} "")
foreach(arg IN LISTS link_args)
  file(APPEND ${OUTPUT_FILE} "${arg}\n")
endforeach()
