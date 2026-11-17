#
# Copyright (c) 2026 Embeint Holdings Pty Ltd
#
# SPDX-License-Identifier: Apache-2.0
#

zephyr_library()

zephyr_library_sources(iis328dq.c)
zephyr_library_sources_ifdef(CONFIG_IIS328DQ_BUS_I2C iis328dq_i2c.c)
zephyr_library_sources_ifdef(CONFIG_IIS328DQ_BUS_SPI iis328dq_spi.c)
