#
# Copyright (c) 2024 Embeint Holdings Pty Ltd
#
# SPDX-License-Identifier: Apache-2.0
#

zephyr_library()

zephyr_library_sources(lsm6dso.c)
zephyr_library_sources_ifdef(CONFIG_LSM6DSO_BUS_I2C lsm6dso_i2c.c)
zephyr_library_sources_ifdef(CONFIG_LSM6DSO_BUS_SPI lsm6dso_spi.c)
