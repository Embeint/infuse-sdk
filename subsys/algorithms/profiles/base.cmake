# SPDX-License-Identifier: FSL-1.1-ALv2
#
# Base algorithm compile flags

set(CFLAGS
  # CPU/Architecture Options
  "-DKERNEL"
  "-D__ZEPHYR__=1"
  "-DLL_EXTENSION_BUILD"
  "-nodefaultlibs"
  "-std=c99"
  # Compile only, no linking
  "-c"
  # Stdlib
  "-specs=picolibc.specs"
  "-fno-common"
  "-fno-strict-aliasing"
  "-fno-defer-pop"
  "-fno-reorder-functions"
  "-fno-printf-return-value"
  "-fno-strict-aliasing"
  # Errors & Warnings
  "-Wall"
  "-Wformat"
  "-Wformat-security"
  "-Wformat"
  "-Wno-format-zero-length"
  "-Wdouble-promotion"
  "-Wno-pointer-sign"
  "-Wpointer-arith"
  "-Wexpansion-to-defined"
  "-Wno-unused-but-set-variable"
  "-Werror=implicit-int"
)
