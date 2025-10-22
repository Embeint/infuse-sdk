/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device_runtime.h>

#include "bma4xx.h"

LOG_MODULE_DECLARE(bma4xx, CONFIG_SENSOR_LOG_LEVEL);

static int bma4xx_bus_check_spi(const union bma4xx_bus *bus)
{
	return spi_is_ready_dt(&bus->spi) ? 0 : -ENODEV;
}

static int bma4xx_bus_pm_spi(const union bma4xx_bus *bus, bool power_up)
{
	if (power_up) {
		return pm_device_runtime_get(bus->spi.bus);
	} else {
		return pm_device_runtime_put(bus->spi.bus);
	}
}

static int bma4xx_reg_read_spi(const union bma4xx_bus *bus, uint8_t reg, uint8_t *data,
			       uint16_t len)
{
	uint8_t addr[2];
	uint8_t tmp[2] = {0x00};
	const struct spi_buf tx_buf = {.buf = addr, .len = 2};
	const struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1};
	struct spi_buf rx_buf[2];
	const struct spi_buf_set rx = {.buffers = rx_buf, .count = ARRAY_SIZE(rx_buf)};
	int ret;

	/* First byte we read should be discarded. */
	rx_buf[0].buf = tmp;
	rx_buf[0].len = 2;
	rx_buf[1].buf = data;
	rx_buf[1].len = len;

	addr[0] = 0x80 | reg;
	addr[1] = 0xFF;

	ret = spi_transceive_dt(&bus->spi, &tx, &rx);
	if (ret < 0) {
		LOG_DBG("spi_transceive failed %i", ret);
	}
	return ret;
}

static int bma4xx_reg_write_spi(const union bma4xx_bus *bus, uint8_t reg, uint8_t data)
{
	int ret;
	const struct spi_buf tx_buf[2] = {{.buf = &reg, .len = sizeof(reg)},
					  {.buf = &data, .len = sizeof(data)}};
	const struct spi_buf_set tx = {.buffers = tx_buf, .count = ARRAY_SIZE(tx_buf)};

	reg = reg & BMA4XX_REG_MASK;

	ret = spi_write_dt(&bus->spi, &tx);
	if (ret < 0) {
		LOG_DBG("spi_write_dt failed %i", ret);
	}
	return ret;
}

static int bma4xx_bus_init_spi(const union bma4xx_bus *bus)
{
	uint8_t tmp;

	/* Single read of SPI initializes the chip to SPI mode */
	return bma4xx_reg_read_spi(bus, BMA4XX_REG_CHIP_ID, &tmp, 1);
}

const struct bma4xx_bus_io bma4xx_bus_io_spi = {
	.check = bma4xx_bus_check_spi,
	.pm = bma4xx_bus_pm_spi,
	.read = bma4xx_reg_read_spi,
	.write = bma4xx_reg_write_spi,
	.init = bma4xx_bus_init_spi,
};
