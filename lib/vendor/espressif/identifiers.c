/* Copyright (c) 2026 Embeint Holdings Pty Ltd
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/byteorder.h>
#include <infuse/identifiers.h>
#ifdef CONFIG_INFUSE_PROVISIONING_ESP32_EFUSE
#include <esp_efuse.h>
#endif

uint64_t vendor_infuse_device_id(void)
{
	uint8_t mac[6];

	if (hwinfo_get_device_id(mac, sizeof(mac)) != sizeof(mac)) {
		return UINT64_MAX - 1;
	}
#ifdef CONFIG_INFUSE_PROVISIONING_ESP32_EFUSE
	uint8_t data[sizeof(uint64_t)];
	uint64_t id;

	if (esp_efuse_read_block(EFUSE_BLK_USER_DATA, data, 0, sizeof(data) * 8) != ESP_OK) {
		return UINT64_MAX - 1;
	}
	id = sys_get_le64(data);
	if (id != 0) {
		if ((id & INFUSE_LOCALLY_MANAGED_PREFIX) == INFUSE_LOCALLY_MANAGED_PREFIX) {
			return UINT64_MAX - 1;
		}
		return id;
	}
#endif
	return INFUSE_LOCALLY_MANAGED_PREFIX | sys_get_be48(mac);
}
