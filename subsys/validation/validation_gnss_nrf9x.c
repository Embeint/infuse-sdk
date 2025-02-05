/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/device.h>
#include <infuse/validation/core.h>
#include <infuse/validation/gnss.h>

#define TEST "GNSS"

int infuse_validation_gnss(const struct device *dev, uint8_t flags)
{
	/* GNSS validation currently only supports power up testing, which doesn't make sense for
	 * the internal nRF9x GNSS.
	 */
	VALIDATION_REPORT_INFO(TEST, "DEV=%s", dev->name);
	VALIDATION_REPORT_PASS(TEST, "DEV=%s", dev->name);
	return 0;
}
