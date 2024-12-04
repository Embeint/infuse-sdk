/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <stdint.h>

uint64_t vendor_infuse_device_id(void)
{
	/* Hardcoded ID */
#ifdef CONFIG_INFUSE_SECURITY_TEST_CREDENTIALS
	return 0xFFFFFFFFFFFFFFFDULL;
#else
	return 0x0000000001000000ULL;
#endif /* CONFIG_INFUSE_SECURITY_TEST_CREDENTIALS */
}
