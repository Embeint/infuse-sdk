/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>

#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

#include "../../../../../subsys/fs/kv_store/kv_internal.h"

#define NVS_PARTITION        storage_partition
#define NVS_PARTITION_DEVICE FIXED_PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET FIXED_PARTITION_OFFSET(NVS_PARTITION)
#define NVS_PARTITION_SIZE   FIXED_PARTITION_SIZE(NVS_PARTITION)

int kv_store_init(void);

ZTEST(kv_store_user, test_key_enabled)
{
	/* Exhaustive check over every key we expect to be enabled */
	for (int i = 0; i <= UINT16_MAX; i++) {
		switch (i) {
		case KV_KEY_REBOOTS:
		case KV_KEY_WIFI_PSK:
		case KV_KEY_LTE_SIM_UICC:
		case KV_KEY_GEOFENCE + 0:
		case KV_KEY_GEOFENCE + 1:
		case KV_KEY_GEOFENCE + 2:
		case KV_KEY_GEOFENCE + 3:
		case KV_KEY_GEOFENCE + 4:
		case KV_KEY_USER_1:
		case KV_KEY_USER_2:
			zassert_true(kv_store_key_enabled(i));
			break;
		default:
			zassert_false(kv_store_key_enabled(i));
			break;
		}
	}
}

ZTEST(kv_store_user, test_key_write_only)
{
	/* Exhaustive check over every key */
	for (int i = 0; i <= UINT16_MAX; i++) {
		switch (i) {
		case KV_KEY_REBOOTS:
		case KV_KEY_LTE_SIM_UICC:
		case KV_KEY_GEOFENCE + 0:
		case KV_KEY_GEOFENCE + 1:
		case KV_KEY_GEOFENCE + 2:
		case KV_KEY_GEOFENCE + 3:
		case KV_KEY_GEOFENCE + 4:
		case KV_KEY_USER_1:
			zassert_equal(0, kv_store_external_write_only(i));
			break;
		case KV_KEY_WIFI_PSK:
		case KV_KEY_USER_2:
			zassert_equal(-EPERM, kv_store_external_write_only(i));
			break;
		default:
			zassert_equal(-EACCES, kv_store_external_write_only(i));
			break;
		}
	}
}

ZTEST(kv_store_user, test_key_read_only)
{
	/* Exhaustive check over every key */
	for (int i = 0; i <= UINT16_MAX; i++) {
		switch (i) {
		case KV_KEY_REBOOTS:
		case KV_KEY_WIFI_PSK:
		case KV_KEY_GEOFENCE + 0:
		case KV_KEY_GEOFENCE + 1:
		case KV_KEY_GEOFENCE + 2:
		case KV_KEY_GEOFENCE + 3:
		case KV_KEY_GEOFENCE + 4:
		case KV_KEY_USER_2:
			zassert_equal(0, kv_store_external_read_only(i));
			break;
		case KV_KEY_LTE_SIM_UICC:
		case KV_KEY_USER_1:
			zassert_equal(-EPERM, kv_store_external_read_only(i));
			break;
		default:
			zassert_equal(-EACCES, kv_store_external_write_only(i));
			break;
		}
	}
}

ZTEST(kv_store_user, test_metadata_reflect_idx)
{
	size_t idx;
	/* Exhaustive check over every key */
	for (int i = 0; i <= UINT16_MAX; i++) {
		kv_store_key_metadata(i, NULL, &idx);

		switch (i) {
		case KV_KEY_REBOOTS:
			zassert_equal(SIZE_MAX, idx);
			break;
		case KV_KEY_WIFI_PSK:
			zassert_equal(0, idx);
			break;
		case KV_KEY_LTE_SIM_UICC:
			zassert_equal(1, idx);
			break;
		case KV_KEY_GEOFENCE + 0:
			zassert_equal(2, idx);
			break;
		case KV_KEY_GEOFENCE + 1:
			zassert_equal(3, idx);
			break;
		case KV_KEY_GEOFENCE + 2:
			zassert_equal(4, idx);
			break;
		case KV_KEY_GEOFENCE + 3:
			zassert_equal(5, idx);
			break;
		case KV_KEY_GEOFENCE + 4:
			zassert_equal(6, idx);
			break;
		case KV_KEY_USER_1:
			zassert_equal(7, idx);
			break;
		case KV_KEY_USER_2:
			zassert_equal(8, idx);
			break;
		default:
			break;
		}
	}
}

struct cb_context {
	int32_t key;
	const void *data;
	size_t data_len;
	int cb_cnt;
};

static void value_changed_callback(uint16_t key, const void *data, size_t data_len, void *user_ctx)
{
	struct cb_context *ctx = user_ctx;

	ctx->key = key;
	ctx->data = data;
	ctx->data_len = data_len;
	ctx->cb_cnt += 1;
}

ZTEST(kv_store_user, test_basic_operation)
{
	static struct cb_context ctx = {0};
	static struct kv_store_cb cb = {
		.value_changed = value_changed_callback,
		.user_ctx = &ctx,
	};
	KV_KEY_TYPE_VAR(KV_KEY_USER_1, 10) user_1 = {0};
	KV_KEY_TYPE(KV_KEY_USER_2) user_2 = {0};
	int rc;

	/* Register for callbacks */
	kv_store_register_callback(&cb);

	ctx.key = -1;
	rc = kv_store_read(KV_KEY_USER_1, &user_1, sizeof(user_1));
	zassert_equal(-ENOENT, rc);
	rc = KV_STORE_READ(KV_KEY_USER_2, &user_2);
	zassert_equal(-ENOENT, rc);
	rc = kv_store_delete(KV_KEY_USER_1);
	zassert_equal(-ENOENT, rc);
	rc = kv_store_delete(KV_KEY_USER_2);
	zassert_equal(-ENOENT, rc);
	zassert_equal(-1, ctx.key);
	zassert_equal(0, ctx.cb_cnt);

	rc = kv_store_write(KV_KEY_USER_1, &user_1, sizeof(user_1));
	zassert_equal(sizeof(user_1), rc);
	zassert_equal(KV_KEY_USER_1, ctx.key);
	zassert_equal(sizeof(user_1), ctx.data_len);
	rc = kv_store_write(KV_KEY_USER_1, &user_1, sizeof(user_1));
	zassert_equal(0, rc);
	rc = KV_STORE_WRITE(KV_KEY_USER_2, &user_2);
	zassert_equal(sizeof(user_2), rc);
	zassert_equal(KV_KEY_USER_2, ctx.key);
	zassert_equal(sizeof(user_2), ctx.data_len);
	rc = KV_STORE_WRITE(KV_KEY_USER_2, &user_2);
	zassert_equal(0, rc);
	zassert_equal(2, ctx.cb_cnt);

	rc = kv_store_read(KV_KEY_USER_1, &user_1, sizeof(user_1));
	zassert_equal(sizeof(user_1), rc);
	rc = KV_STORE_READ(KV_KEY_USER_2, &user_2);
	zassert_equal(sizeof(user_2), rc);

	rc = kv_store_delete(KV_KEY_USER_1);
	zassert_equal(0, rc);
	rc = kv_store_delete(KV_KEY_USER_2);
	zassert_equal(0, rc);
	zassert_equal(4, ctx.cb_cnt);
}

ZTEST(kv_store_user, test_kv_reflect_crc)
{
	KV_KEY_TYPE_VAR(KV_KEY_USER_1, 10) user_1 = {0};
	KV_KEY_TYPE(KV_KEY_USER_2) user_2 = {0};
	uint32_t reflect_crc, prev_crc, initial_crc;
	int rc;

	/* Initial value */
	reflect_crc = kv_store_reflect_crc();
	initial_crc = reflect_crc;
	zassert_not_equal(0x00, reflect_crc);

	/* Write to key that is reflected */
	prev_crc = reflect_crc;
	rc = kv_store_write(KV_KEY_USER_1, &user_1, sizeof(user_1));
	zassert_equal(sizeof(user_1), rc);
	reflect_crc = kv_store_reflect_crc();
	zassert_not_equal(prev_crc, reflect_crc);

	/* Write to key that is reflected */
	prev_crc = reflect_crc;
	rc = KV_STORE_WRITE(KV_KEY_USER_2, &user_2);
	zassert_equal(sizeof(user_2), rc);
	reflect_crc = kv_store_reflect_crc();
	zassert_not_equal(prev_crc, reflect_crc);
}

static void *kv_setup(void)
{
	kv_store_init();
	return NULL;
}

static void kv_before(void *fixture)
{
	kv_store_reset();
}

ZTEST_SUITE(kv_store_user, NULL, kv_setup, kv_before, NULL, NULL);
