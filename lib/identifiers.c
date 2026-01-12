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
#if defined(CONFIG_INFUSE_TEST_ID)
	return INFUSE_TEST_DEVICE_ID;
#elif defined(CONFIG_INFUSE_CACHE_DEVICE_ID)
	static uint64_t cached;

	if (cached == 0) {
		/* ID is cached to avoid needing to go to OTP on every call.
		 * This could be expensive depending on the `vendor_infuse_device_id`
		 * implementation.
		 */
		cached = vendor_infuse_device_id();
	}
	return cached;
#else
	return vendor_infuse_device_id();
#endif /* CONFIG_INFUSE_TEST_ID */
}
