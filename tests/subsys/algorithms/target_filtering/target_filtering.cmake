# SPDX-License-Identifier: FSL-1.1-ALv2

# Whitelist to limit to hard ABI targets only.
# Restrict to m33/m4 cores only.
list(APPEND CORE_PROFILES
  "cortex_m33"
  "cortex_m4"
)

# Limit FP mode to "hard" only
set(CORE_FP_MODES
  "hard"
)

include(${INFUSE_SDK_BASE}/subsys/algorithms/build_helpers.cmake)

function(get_targets_whitelist_only
  WHITELIST
)
  generate_targets_whitelist(
    "${CORE_PROFILES}"
    "${CORE_FP_MODES}"
    whitelist_only
  )
  set(${WHITELIST} ${whitelist_only} PARENT_SCOPE)
endfunction()
