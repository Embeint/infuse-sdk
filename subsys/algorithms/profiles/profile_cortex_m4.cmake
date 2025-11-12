# SPDX-License-Identifier: FSL-1.1-ALv2
#
# Cortex-M4 compile flags

# Include the base flags
include(${CMAKE_CURRENT_LIST_DIR}/base.cmake)

list(APPEND CFLAGS
  # CPU/Architecture Options
  "-mcpu=cortex-m4"
  "-mthumb"
  "-mabi=aapcs"
  "-mfp16-format=ieee"
  "-mlong-calls"
  # Required Kconfigs
  "-DCONFIG_ARM=1"
)

set(PROFILE_NAME "cortex_m4")
set(PROFILE_FP_MODES
  "soft"
  "softfp"
  "hard"
)
