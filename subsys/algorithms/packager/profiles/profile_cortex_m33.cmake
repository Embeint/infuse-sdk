# SPDX-License-Identifier: FSL-1.1-ALv2
#
# Cortex-M33 compile flags

# Include the base flags
include(${CMAKE_CURRENT_LIST_DIR}/base.cmake)

list(APPEND CFLAGS
  # CPU/Architecture Options
  "-mcpu=cortex-m33"
  "-mthumb"
  "-mabi=aapcs"
  "-mfp16-format=ieee"
  "-mlong-calls"
  # Required Kconfigs
  "-DCONFIG_ARM=1"
)

set(PROFILE_NAME "cortex_m33")
set(PROFILE_FP_MODES
  "soft"
  "softfp"
  "hard"
)
