/*
 * Copyright (c) 2026 Embeint Holdings Pty Ltd
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include <infuse/identifiers.h>

static const uint8_t mac[] = {0x7c, 0x2c, 0x67, 0x7c, 0x82, 0x80};
static ssize_t hwinfo_result;
static uint8_t efuse_id[sizeof(uint64_t)];
static int efuse_result;

ssize_t z_impl_hwinfo_get_device_id(uint8_t *buffer, size_t length)
{
	zassert_equal(length, sizeof(mac));
	memcpy(buffer, mac, MIN(length, sizeof(mac)));
	return hwinfo_result;
}

int esp_efuse_read_block(int block, void *data, size_t offset, size_t bits)
{
	zassert_equal(block, 3);
	zassert_equal(offset, 0);
	zassert_equal(bits, 64);
	memcpy(data, efuse_id, sizeof(efuse_id));
	return efuse_result;
}

ZTEST(espressif_identifiers, test_blank_efuses_use_factory_mac)
{
	zassert_equal(vendor_infuse_device_id(), 0xffff7c2c677c8280ULL);
}

ZTEST(espressif_identifiers, test_infuse_id_is_little_endian)
{
	const uint8_t data[] = {0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12};

	memcpy(efuse_id, data, sizeof(data));
	zassert_equal(vendor_infuse_device_id(), 0x123456789abcdef0ULL);
}

ZTEST(espressif_identifiers, test_reserved_namespace_is_invalid)
{
	const uint64_t invalid[] = {UINT64_MAX, UINT64_MAX - 1, 0xffff7c2c677c8280ULL};

	for (size_t i = 0; i < ARRAY_SIZE(invalid); i++) {
		sys_put_le64(invalid[i], efuse_id);
		zassert_equal(vendor_infuse_device_id(), UINT64_MAX - 1);
	}
}

ZTEST(espressif_identifiers, test_short_hardware_id_is_invalid)
{
	hwinfo_result = sizeof(mac) - 1;
	zassert_equal(vendor_infuse_device_id(), UINT64_MAX - 1);
}

ZTEST(espressif_identifiers, test_hwinfo_failure_is_invalid)
{
	hwinfo_result = -EIO;
	zassert_equal(vendor_infuse_device_id(), UINT64_MAX - 1);
}

ZTEST(espressif_identifiers, test_efuse_read_failure_does_not_fall_back)
{
	efuse_result = -EIO;
	zassert_equal(vendor_infuse_device_id(), UINT64_MAX - 1);
}

static void before(void *fixture)
{
	hwinfo_result = sizeof(mac);
	efuse_result = 0;
	memset(efuse_id, 0, sizeof(efuse_id));
}

ZTEST_SUITE(espressif_identifiers, NULL, NULL, before, NULL, NULL);
