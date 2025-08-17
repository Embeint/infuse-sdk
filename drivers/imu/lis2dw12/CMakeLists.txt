#
# Copyright (c) 2025 Embeint Holdings Pty Ltd
#
# SPDX-License-Identifier: Apache-2.0
#

zephyr_library()

zephyr_library_sources(lis2dw12.c)
zephyr_library_sources_ifdef(CONFIG_LIS2DW12_BUS_I2C lis2dw12_i2c.c)
zephyr_library_sources_ifdef(CONFIG_LIS2DW12_BUS_SPI lis2dw12_spi.c)
