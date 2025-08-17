/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <errno.h>

#include <zephyr/device.h>

#include "lsm6dso.h"

static int lsm6dso_bus_check_spi(const union lsm6dso_bus *bus)
{
	return spi_is_ready_dt(&bus->spi) ? 0 : -ENODEV;
}

static int lsm6dso_reg_read_spi(const union lsm6dso_bus *bus, uint8_t start, uint8_t *data,
				uint16_t len)
{
	uint8_t addr;
	const struct spi_buf tx_buf = {.buf = &addr, .len = 1};
	const struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1};
	struct spi_buf rx_buf[2];
	const struct spi_buf_set rx = {.buffers = rx_buf, .count = ARRAY_SIZE(rx_buf)};

	rx_buf[0].buf = NULL;
	rx_buf[0].len = 1;
	rx_buf[1].len = len;
	rx_buf[1].buf = data;

	addr = start | 0x80;

	return spi_transceive_dt(&bus->spi, &tx, &rx);
}

static int lsm6dso_reg_write_spi(const union lsm6dso_bus *bus, uint8_t start, const uint8_t *data,
				 uint16_t len)
{
	uint8_t addr;
	const struct spi_buf tx_buf[2] = {{.buf = &addr, .len = sizeof(addr)},
					  {.buf = (uint8_t *)data, .len = len}};
	const struct spi_buf_set tx = {.buffers = tx_buf, .count = ARRAY_SIZE(tx_buf)};

	addr = start & 0x7F;

	return spi_write_dt(&bus->spi, &tx);
}

static int lsm6dso_bus_init_spi(const union lsm6dso_bus *bus)
{
	ARG_UNUSED(bus);

	return 0;
}

const struct lsm6dso_bus_io lsm6dso_bus_io_spi = {
	.check = lsm6dso_bus_check_spi,
	.read = lsm6dso_reg_read_spi,
	.write = lsm6dso_reg_write_spi,
	.init = lsm6dso_bus_init_spi,
};
