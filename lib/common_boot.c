/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <infuse/identifiers.h>

LOG_MODULE_REGISTER(infuse, CONFIG_INFUSE_COMMON_LOG_LEVEL);

static int infuse_common_boot(void)
{
	uint32_t device_id = infuse_device_id();

	LOG_INF("\tDevice: %08x", device_id);
	LOG_INF("\t Board: %s", CONFIG_BOARD);
	return 0;
}

SYS_INIT(infuse_common_boot, APPLICATION, 10);
