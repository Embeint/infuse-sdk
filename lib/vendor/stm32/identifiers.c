/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <infuse/identifiers.h>

#ifdef CONFIG_SOC_SERIES_STM32L4X
#define FLASH_OTP_BASE 0x1FFF7000
#else
#error Unknown SOC series
#endif

struct stm32_otp_structure {
	uint64_t infuse_device_id;
};

uint64_t vendor_infuse_device_id(void)
{
	struct stm32_otp_structure readout;

	memcpy(&readout, (const void *)FLASH_OTP_BASE, sizeof(readout));

	return readout.infuse_device_id;
}
