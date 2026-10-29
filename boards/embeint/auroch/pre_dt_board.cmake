# Copyright (c) 2021 Linaro Limited
# SPDX-License-Identifier: Apache-2.0

# Suppress "unique_unit_address_if_enabled" to handle the following overlaps:
# - flash-controller@39000 & kmu@39000
# - power@5000 & clock@5000
# - power@X010e000 & clock@X010e000 & xo@X010e000 & lfclk@X010e000
list(APPEND EXTRA_DTC_FLAGS "-Wno-unique_unit_address_if_enabled")
