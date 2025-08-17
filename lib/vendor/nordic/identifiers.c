/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <infuse/identifiers.h>

#include <soc_secure.h>
#include <nrf.h>

#if defined(NRF_UICR_S)
#define UICR_PTR NRF_UICR_S
#define UICR_REG OTP
#else
#define UICR_PTR NRF_UICR
#define UICR_REG CUSTOMER
#endif

struct nrf_uicr_structure {
	uint64_t infuse_device_id;
};

uint64_t vendor_infuse_device_id(void)
{
#ifdef CONFIG_BUILD_WITH_TFM
	static struct nrf_uicr_structure readout;
	static bool queried;

	if (queried == false) {
		if (soc_secure_mem_read(&readout, (void *)UICR_PTR->UICR_REG, sizeof(readout)) <
		    0) {
			readout.infuse_device_id = UINT64_MAX - 1;
		}
		queried = true;
	}
#else
	struct nrf_uicr_structure readout;

	memcpy(&readout, (const void *)UICR_PTR->UICR_REG, sizeof(readout));
#endif

	return readout.infuse_device_id;
}
