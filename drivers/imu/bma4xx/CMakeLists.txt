# Copyright (c) 2024 Embeint Holdings Pty Ltd
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(bma4xx.c)
zephyr_library_sources_ifdef(CONFIG_BMA4XX_BUS_SPI bma4xx_spi.c)
