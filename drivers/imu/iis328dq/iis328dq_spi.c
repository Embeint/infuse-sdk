/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <errno.h>

#include <zephyr/device.h>

#include "iis328dq.h"

#define IIS328DQ_SPI_READ           BIT(7)
#define IIS328DQ_SPI_AUTO_INCREMENT BIT(6)

static int iis328dq_bus_check_spi(const union iis328dq_bus *bus)
{
	return spi_is_ready_dt(&bus->spi) ? 0 : -ENODEV;
}

static int iis328dq_reg_read_spi(const union iis328dq_bus *bus, uint8_t start, uint8_t *data,
				 uint16_t len)
{
	uint8_t addr = start | IIS328DQ_SPI_READ | (len > 1 ? IIS328DQ_SPI_AUTO_INCREMENT : 0);
	const struct spi_buf tx_buf = {.buf = &addr, .len = 1};
	const struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1};
	struct spi_buf rx_buf[2] = {
		{.buf = NULL, .len = 1},
		{.buf = data, .len = len},
	};
	const struct spi_buf_set rx = {.buffers = rx_buf, .count = ARRAY_SIZE(rx_buf)};

	return spi_transceive_dt(&bus->spi, &tx, &rx);
}

static int iis328dq_reg_write_spi(const union iis328dq_bus *bus, uint8_t start, const uint8_t *data,
				  uint16_t len)
{
	uint8_t addr = (start & 0x3F) | (len > 1 ? IIS328DQ_SPI_AUTO_INCREMENT : 0);
	const struct spi_buf tx_buf[2] = {
		{.buf = &addr, .len = 1},
		{.buf = (uint8_t *)data, .len = len},
	};
	const struct spi_buf_set tx = {.buffers = tx_buf, .count = ARRAY_SIZE(tx_buf)};

	return spi_write_dt(&bus->spi, &tx);
}

const struct iis328dq_bus_io iis328dq_bus_io_spi = {
	.check = iis328dq_bus_check_spi,
	.read = iis328dq_reg_read_spi,
	.write = iis328dq_reg_write_spi,
};
