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

static int lsm6dso_bus_check_i2c(const union lsm6dso_bus *bus)
{
	return device_is_ready(bus->i2c.bus) ? 0 : -ENODEV;
}

static int lsm6dso_reg_read_i2c(const union lsm6dso_bus *bus, uint8_t start, uint8_t *data,
				uint16_t len)
{
	return i2c_burst_read_dt(&bus->i2c, start, data, len);
}

static int lsm6dso_reg_write_i2c(const union lsm6dso_bus *bus, uint8_t start, const uint8_t *data,
				 uint16_t len)
{
	return i2c_burst_write_dt(&bus->i2c, start, data, len);
}

static int lsm6dso_bus_init_i2c(const union lsm6dso_bus *bus)
{
	ARG_UNUSED(bus);

	return 0;
}

const struct lsm6dso_bus_io lsm6dso_bus_io_i2c = {
	.check = lsm6dso_bus_check_i2c,
	.read = lsm6dso_reg_read_i2c,
	.write = lsm6dso_reg_write_i2c,
	.init = lsm6dso_bus_init_i2c,
};
