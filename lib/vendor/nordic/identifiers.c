/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <string.h>

#include <infuse/identifiers.h>

#include <nrf.h>

#ifdef CONFIG_ARM_TRUSTZONE_M
#define UICR_PTR NRF_UICR_S
#define UICR_REG OTP
#else
#define UICR_PTR NRF_UICR
#define UICR_REG CUSTOMER
#endif

struct nrf_uicr_structure {
	uint64_t infuse_device_id;
	uint8_t infuse_identifier_secret[16];
};

uint64_t infuse_device_id(void)
{
	struct nrf_uicr_structure readout;

	memcpy(&readout, (const void *)UICR_PTR->UICR_REG, sizeof(readout));

	return readout.infuse_device_id;
}
