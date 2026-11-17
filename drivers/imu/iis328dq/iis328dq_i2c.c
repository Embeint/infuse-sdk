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

/* MSb of the register address enables address auto-increment */
#define IIS328DQ_I2C_AUTO_INCREMENT BIT(7)

static int iis328dq_bus_check_i2c(const union iis328dq_bus *bus)
{
	return i2c_is_ready_dt(&bus->i2c) ? 0 : -ENODEV;
}

static int iis328dq_reg_read_i2c(const union iis328dq_bus *bus, uint8_t start, uint8_t *data,
				 uint16_t len)
{
	return i2c_burst_read_dt(&bus->i2c, start | IIS328DQ_I2C_AUTO_INCREMENT, data, len);
}

static int iis328dq_reg_write_i2c(const union iis328dq_bus *bus, uint8_t start, const uint8_t *data,
				  uint16_t len)
{
	return i2c_burst_write_dt(&bus->i2c, start | IIS328DQ_I2C_AUTO_INCREMENT, data, len);
}

const struct iis328dq_bus_io iis328dq_bus_io_i2c = {
	.check = iis328dq_bus_check_i2c,
	.read = iis328dq_reg_read_i2c,
	.write = iis328dq_reg_write_i2c,
};
