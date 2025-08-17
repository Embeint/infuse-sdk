#
# Copyright (c) 2024 Embeint Holdings Pty Ltd
#
# SPDX-License-Identifier: Apache-2.0
#

zephyr_library()

zephyr_library_sources(lsm6dsv.c)
zephyr_library_sources_ifdef(CONFIG_LSM6DSV_BUS_I2C lsm6dsv_i2c.c)
zephyr_library_sources_ifdef(CONFIG_LSM6DSV_BUS_SPI lsm6dsv_spi.c)
