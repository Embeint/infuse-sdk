/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <em_device.h>

#include <infuse/identifiers.h>

struct silabs_userdata_structure {
	uint64_t infuse_device_id;
};

uint64_t vendor_infuse_device_id(void)
{
	struct silabs_userdata_structure readout;

	memcpy(&readout, (const void *)USERDATA_BASE, sizeof(readout));

	if (readout.infuse_device_id == UINT64_MAX) {
		/* Device not provisioned, generate a locally administered address from the
		 * factory programmed EUI48, formatted as a static random Bluetooth address.
		 */
		const uint64_t eui48_h = DEVINFO->EUI48H & _DEVINFO_EUI48H_OUI48H_MASK;
		const uint64_t eui48 = (eui48_h << 32) | DEVINFO->EUI48L;

		return local_infuse_device_id_from_bt(BLUETOOTH_STATIC_RANDOM_PREFIX | eui48);
	}

	return readout.infuse_device_id;
}
