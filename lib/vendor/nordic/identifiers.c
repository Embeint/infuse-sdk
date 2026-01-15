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
#define FICR_PTR NRF_FICR_NS
#define UICR_PTR NRF_UICR_S
#define UICR_REG OTP
#else
#define FICR_PTR NRF_FICR
#define UICR_PTR NRF_UICR
#define UICR_REG CUSTOMER
#endif

#if defined(CONFIG_SOC_SERIES_NRF52X) || defined(CONFIG_SOC_SERIES_NRF54LX)
#define HAS_DEVICEADDR 1
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

#ifdef HAS_DEVICEADDR
	if (readout.infuse_device_id == UINT64_MAX) {
		/* Device not provisioned, generate a locally administered address fom the Bluetooth
		 * address
		 */
		const uint64_t bt_addr = BLUETOOTH_STATIC_RANDOM_PREFIX |
					 (uint64_t)(FICR_PTR->DEVICEADDR[1] & 0xFFFF) << 32 |
					 FICR_PTR->DEVICEADDR[0];

		return local_infuse_device_id_from_bt(bt_addr);
	}
#endif /* HAS_DEVICEADDR */

	return readout.infuse_device_id;
}
