# SPDX-License-Identifier: FSL-1.1-ALv2
#
# Common helpers to include libraries.

# Generate a list of the target permutations.
function(generate_targets_whitelist
  CORE_PROFILES
  FP_ABIS
  TARGETS_WHITELIST
)
  foreach(CORE_PROFILE IN LISTS CORE_PROFILES)
    foreach(FP_ABI IN LISTS FP_ABIS)
      set(target_name "${CORE_PROFILE}-${FP_ABI}")
      list(APPEND TARGETS_WHITELIST_TEMP ${target_name})
    endforeach()
  endforeach()
  message(DEBUG "Generated targets whitelist: ${TARGETS_WHITELIST_TEMP}")
  set(${TARGETS_WHITELIST} ${TARGETS_WHITELIST_TEMP} PARENT_SCOPE)
endfunction()
