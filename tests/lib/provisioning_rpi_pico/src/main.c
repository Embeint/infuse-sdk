/**
 * @copyright 2026 Embeint Holdings Pty Ltd
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>
#include <zephyr/ztest.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/byteorder.h>
#include <infuse/identifiers.h>

bool infuse_pico_provisioned_id(const uint8_t *record, uint64_t *id);
uint64_t vendor_infuse_device_id(void);
uint64_t infuse_pico_device_id(const uint8_t *record);

static const uint8_t hardware_id[] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0};
static int hwinfo_calls;

ssize_t z_impl_hwinfo_get_device_id(uint8_t *buffer, size_t length)
{
	hwinfo_calls++;
	memcpy(buffer, hardware_id, MIN(length, sizeof(hardware_id)));
	return MIN(length, sizeof(hardware_id));
}

/* Shared golden vector with python-tools tests/util/test_soc_rpi.py. */
static const uint8_t golden[] = {
	0x49, 0x46, 0x50, 0x49, 0x01, 0x00, 0x00, 0x00, 0x66, 0x55, 0x44, 0x33,
	0x22, 0x11, 0x00, 0xab, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x54,
};

ZTEST(provisioning_rpi_pico, test_record)
{
	uint64_t id = 0;

	zassert_true(infuse_pico_provisioned_id(golden, &id));
	zassert_equal(id, 0xab00112233445566ULL);
}

ZTEST(provisioning_rpi_pico, test_erased_corrupt_and_incomplete)
{
	uint8_t record[sizeof(golden)];
	uint64_t id = 123;

	for (size_t len = 0; len < sizeof(record); len++) {
		memset(record, 0xff, sizeof(record));
		memcpy(record, golden, len);
		zassert_false(infuse_pico_provisioned_id(record, &id));
		zassert_equal(id, 123);
	}
	for (size_t byte = 0; byte < sizeof(record); byte++) {
		for (int bit = 0; bit < 8; bit++) {
			memcpy(record, golden, sizeof(record));
			record[byte] ^= BIT(bit);
			zassert_false(infuse_pico_provisioned_id(record, &id));
		}
	}
}

ZTEST(provisioning_rpi_pico, test_reserved_ids)
{
	const uint64_t invalid[] = {0, UINT64_MAX, 0xffff000000000001ULL};
	uint8_t record[sizeof(golden)];
	uint64_t id;

	memcpy(record, golden, sizeof(record));
	for (size_t i = 0; i < ARRAY_SIZE(invalid); i++) {
		sys_put_le64(invalid[i], record + 8);
		sys_put_le64(~invalid[i], record + 16);
		zassert_false(infuse_pico_provisioned_id(record, &id));
	}
}

ZTEST(provisioning_rpi_pico, test_fallback_and_hardware_id_unchanged)
{
	uint8_t before[8];
	uint8_t after[8];
	uint8_t erased[24];
	uint64_t provisioned;

	hwinfo_calls = 0;
	z_impl_hwinfo_get_device_id(before, sizeof(before));
	zassert_equal(vendor_infuse_device_id(), 0xffff56789abcdef0ULL);
	zassert_true(infuse_pico_provisioned_id(golden, &provisioned));
	zassert_equal(infuse_pico_device_id(golden), provisioned);
	zassert_equal(hwinfo_calls, 2);
	z_impl_hwinfo_get_device_id(after, sizeof(after));
	zassert_mem_equal(before, after, sizeof(before));
	zassert_equal(hwinfo_calls, 3);
	memset(erased, 0xff, sizeof(erased));
	zassert_equal(infuse_pico_device_id(erased), 0xffff56789abcdef0ULL);
	zassert_equal(hwinfo_calls, 4);
}

ZTEST_SUITE(provisioning_rpi_pico, NULL, NULL, NULL, NULL, NULL);
