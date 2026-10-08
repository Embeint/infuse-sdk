/**
 * @copyright 2026 Embeint Holdings Pty Ltd
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/__assert.h>

#include <infuse/identifiers.h>

/* USB provisioning tools write this record at the start of the dedicated
 * 4 KiB partition. All fields are little-endian; unused bytes remain erased.
 * The complemented ID rejects incomplete programming and single-bit errors.
 */
#define INFUSE_PICO_PROVISIONING_MAGIC   0x49504649U /* "IFPI" */
#define INFUSE_PICO_PROVISIONING_VERSION 1U
#define INFUSE_PICO_PROVISIONING_SIZE    24U

bool infuse_pico_provisioned_id(const uint8_t record[INFUSE_PICO_PROVISIONING_SIZE], uint64_t *id)
{
	uint64_t value = sys_get_le64(record + 8);

	if ((sys_get_le32(record) != INFUSE_PICO_PROVISIONING_MAGIC) ||
	    (sys_get_le32(record + 4) != INFUSE_PICO_PROVISIONING_VERSION) || (value == 0) ||
	    ((value >> 48) == 0xffff) || (sys_get_le64(record + 16) != ~value)) {
		return false;
	}
	*id = value;
	return true;
}

uint64_t infuse_pico_device_id(const uint8_t *record)
{
	uint64_t provisioned;

	if (record != NULL && infuse_pico_provisioned_id(record, &provisioned)) {
		return provisioned;
	}
	uint8_t id[8];
	ssize_t rc = hwinfo_get_device_id(id, sizeof(id));

	__ASSERT(rc == sizeof(id), "Unable to read Pico flash identifier");
	if (rc != sizeof(id)) {
		return UINT64_MAX - 1;
	}

	/* HWINFO returns big-endian IDs; retain the low 48 bits in the local namespace. */
	return INFUSE_LOCALLY_MANAGED_PREFIX | (sys_get_be64(id) & 0x0000FFFFFFFFFFFFULL);
}

uint64_t vendor_infuse_device_id(void)
{
#if DT_HAS_CHOSEN(infuse_provisioning)
	/* RP-series flash is memory mapped. Reading needs no flash-driver init. */
	const uint8_t *record = (const uint8_t *)(DT_REG_ADDR(DT_CHOSEN(zephyr_flash)) +
						  DT_REG_ADDR(DT_CHOSEN(infuse_provisioning)));

	BUILD_ASSERT(DT_REG_SIZE(DT_CHOSEN(infuse_provisioning)) >= INFUSE_PICO_PROVISIONING_SIZE);
	return infuse_pico_device_id(record);
#else
	return infuse_pico_device_id(NULL);
#endif
}
