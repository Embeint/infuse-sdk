/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdint.h>

#if defined(CONFIG_SOC_SERIES_BSIM_NRFXX)
#include <nrf.h>
#endif

uint64_t vendor_infuse_device_id(void)
{
	/* Hardcoded ID */
#if defined(CONFIG_INFUSE_SECURITY_TEST_CREDENTIALS)
	return 0xFFFFFFFFFFFFFFFDULL;
#elif defined(CONFIG_SOC_SERIES_BSIM_NRFXX)
	return 0xB000000000000000ULL + NRF_FICR->DEVICEADDR[0];
#else
	return 0x0000000001000000ULL;
#endif /* CONFIG_INFUSE_SECURITY_TEST_CREDENTIALS */
}
