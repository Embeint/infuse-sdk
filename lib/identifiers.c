/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <infuse/identifiers.h>

uint64_t infuse_device_id(void)
{
#ifdef CONFIG_INFUSE_TEST_ID
	return INFUSE_TEST_DEVICE_ID;
#else
	return vendor_infuse_device_id();
#endif /* CONFIG_INFUSE_TEST_ID */
}
