/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <infuse/identifiers.h>

uint64_t infuse_device_id(void)
{
#ifdef CONFIG_BOARD_THINGY53_NRF5340_CPUAPP
	return 1235;
#else
	return 1234;
#endif
}
