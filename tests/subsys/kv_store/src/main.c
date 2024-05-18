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

#define NVS_PARTITION        storage_partition
#define NVS_PARTITION_DEVICE FIXED_PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET FIXED_PARTITION_OFFSET(NVS_PARTITION)
#define NVS_PARTITION_SIZE   FIXED_PARTITION_SIZE(NVS_PARTITION)

ZTEST(kv_store, test_init_failure)
{
	const struct device *dev = NVS_PARTITION_DEVICE;
	uint8_t zeroes[64] = {0x00};

	/* Write all the flash to 0 */
	for (int i = 0; i < NVS_PARTITION_SIZE; i += sizeof(zeroes)) {
		zassert_equal(0, flash_write(dev, NVS_PARTITION_OFFSET + i, zeroes, sizeof(zeroes)));
	}

	/* Ensure init still succeeds */
	zassert_equal(0, kv_store_init());
}

ZTEST(kv_store, test_key_enabled)
{
	/* Exhaustive check over every key we expect to be enabled */
	for (int i = 0; i <= UINT16_MAX; i++) {
		switch (i) {
		case KV_KEY_REBOOTS:
		case KV_KEY_WIFI_PSK:
		case KV_KEY_GEOFENCE + 0:
		case KV_KEY_GEOFENCE + 1:
		case KV_KEY_GEOFENCE + 2:
		case KV_KEY_GEOFENCE + 3:
		case KV_KEY_GEOFENCE + 4:
			zassert_true(kv_store_key_enabled(i));
			break;
		default:
			zassert_false(kv_store_key_enabled(i));
			break;
		}
	}
}

ZTEST(kv_store, test_disabled_key)
{
	struct kv_fixed_location location, fallback;
	ssize_t rc;

	rc = kv_store_read(KV_KEY_FIXED_LOCATION, &location, sizeof(location));
	zassert_equal(-EACCES, rc);
	rc = kv_store_read_fallback(KV_KEY_FIXED_LOCATION, &location, sizeof(location), &fallback, sizeof(fallback));
	zassert_equal(-EACCES, rc);
	rc = kv_store_write(KV_KEY_FIXED_LOCATION, &location, sizeof(location));
	zassert_equal(-EACCES, rc);
	rc = kv_store_delete(KV_KEY_FIXED_LOCATION);
	zassert_equal(-EACCES, rc);
}

ZTEST(kv_store, test_basic_operation)
{
	struct kv_reboots reboots;
	ssize_t rc;

	/* Enabled key not yet written */
	rc = kv_store_read(KV_KEY_REBOOTS, &reboots, sizeof(reboots));
	zassert_equal(-ENOENT, rc);
	rc = kv_store_delete(KV_KEY_REBOOTS);
	zassert_equal(-ENOENT, rc);

	/* Basic write, write duplicate, write new sequence */
	reboots.count = 10;
	rc = kv_store_write(KV_KEY_REBOOTS, &reboots, sizeof(reboots));
	zassert_equal(sizeof(reboots), rc);
	rc = kv_store_write(KV_KEY_REBOOTS, &reboots, sizeof(reboots));
	zassert_equal(0, rc);
	reboots.count = 11;
	rc = kv_store_write(KV_KEY_REBOOTS, &reboots, sizeof(reboots));
	zassert_equal(sizeof(reboots), rc);

	/* Validate written data */
	rc = kv_store_read(KV_KEY_REBOOTS, &reboots, sizeof(reboots));
	zassert_equal(sizeof(reboots), rc);
	zassert_equal(11, reboots.count);

	/* Delete and try to read */
	rc = kv_store_delete(KV_KEY_REBOOTS);
	zassert_equal(0, rc);
	rc = kv_store_read(KV_KEY_REBOOTS, &reboots, sizeof(reboots));
	zassert_equal(-ENOENT, rc);
}

ZTEST(kv_store, test_basic_macro_helper)
{
	KV_STRING_CONST(fallback, "small_fallback");
	KV_KEY_TYPE_VAR(KV_KEY_WIFI_PSK, 32) psk;
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots;
	ssize_t rc;

	zassert_equal(sizeof(reboots), sizeof(struct kv_reboots));

	reboots.count = 15;
	rc = KV_STORE_WRITE(KV_KEY_REBOOTS, &reboots);
	zassert_equal(sizeof(reboots), rc);

	rc = KV_STORE_READ(KV_KEY_REBOOTS, &reboots);
	zassert_equal(sizeof(reboots), rc);
	zassert_equal(15, reboots.count);

	rc = KV_STORE_READ_FALLBACK(KV_KEY_WIFI_PSK, &psk, &fallback);
	zassert_equal(sizeof(fallback), rc);
}

ZTEST(kv_store, test_read_fallback)
{
	struct kv_reboots fallback = {.count = 100};
	struct kv_reboots reboots;
	ssize_t rc;

	/* Initial fallback read */
	rc = kv_store_read_fallback(KV_KEY_REBOOTS, &reboots, sizeof(reboots), &fallback, sizeof(fallback));
	zassert_equal(sizeof(reboots), rc);
	zassert_equal(100, reboots.count);

	/* Write new value */
	reboots.count += 10;
	rc = kv_store_write(KV_KEY_REBOOTS, &reboots, sizeof(reboots));
	zassert_equal(sizeof(reboots), rc);

	/* Second fallback read */
	rc = kv_store_read_fallback(KV_KEY_REBOOTS, &reboots, sizeof(reboots), &fallback, sizeof(fallback));
	zassert_equal(sizeof(reboots), rc);
	zassert_equal(110, reboots.count);
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

ZTEST(kv_store, test_callbacks)
{
	static struct cb_context ctx;
	static struct kv_store_cb cb = {
		.value_changed = value_changed_callback,
		.user_ctx = &ctx,
	};
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots = {0};
	KV_KEY_TYPE(KV_KEY_REBOOTS) fallback[2] = {{100}, {101}};

	/* Register for callbacks */
	kv_store_register_callback(&cb);

	/* Callback not run if key doesn't exist */
	ctx.key = -1;
	(void)kv_store_delete(KV_KEY_REBOOTS);
	zassert_equal(-1, ctx.key);
	zassert_equal(0, ctx.cb_cnt);

	/* Callback run on write */
	(void)KV_STORE_WRITE(KV_KEY_REBOOTS, &reboots);
	zassert_equal(KV_KEY_REBOOTS, ctx.key);
	zassert_not_null(ctx.data);
	zassert_equal(sizeof(reboots), ctx.data_len);
	zassert_equal(1, ctx.cb_cnt);

	/* Callback not run on duplicate data */
	ctx.key = -1;
	(void)KV_STORE_WRITE(KV_KEY_REBOOTS, &reboots);
	zassert_equal(-1, ctx.key);
	zassert_equal(1, ctx.cb_cnt);

	/* Callback run on delete */
	ctx.key = -1;
	(void)kv_store_delete(KV_KEY_REBOOTS);
	zassert_equal(KV_KEY_REBOOTS, ctx.key);
	zassert_is_null(ctx.data);
	zassert_equal(0, ctx.data_len);
	zassert_equal(2, ctx.cb_cnt);

	/* Callback run on fallback */
	ctx.key = -1;
	(void)kv_store_read_fallback(KV_KEY_REBOOTS, &reboots, sizeof(reboots), &fallback, sizeof(fallback));
	zassert_equal(KV_KEY_REBOOTS, ctx.key);
	zassert_not_null(ctx.data);
	zassert_equal(sizeof(fallback), ctx.data_len);
	zassert_equal(3, ctx.cb_cnt);
}

ZTEST(kv_store, test_kv_var_macro)
{
	KV_KEY_TYPE_VAR(KV_KEY_GEOFENCE, 2) geofence = {2, {{1, 2, 3}, {4, 5, 6}}};
	KV_KEY_TYPE_VAR(KV_KEY_WIFI_PSK, 16) psk;

	zassert_equal(2, ARRAY_SIZE(geofence.points));
	zassert_equal(2, geofence.points_num);
	zassert_equal(1, geofence.points[0].latitude);
	zassert_equal(2, geofence.points[0].longitude);
	zassert_equal(3, geofence.points[0].height);
	zassert_equal(4, geofence.points[1].latitude);
	zassert_equal(5, geofence.points[1].longitude);
	zassert_equal(6, geofence.points[1].height);

	zassert_equal(16, ARRAY_SIZE(psk.psk.value));
	zassert_equal(17, sizeof(psk));
}

ZTEST(kv_store, test_kv_string_helper)
{
	KV_STRING_CONST(string_1, "my_network_name");
	KV_STRING_CONST(string_2, "my_network_password");

	zassert_equal(16, ARRAY_SIZE(string_1.value));
	zassert_equal(20, ARRAY_SIZE(string_2.value));
	zassert_equal(sizeof(string_1), sizeof(string_1.value) + 1);
	zassert_equal(sizeof(string_2), sizeof(string_2.value) + 1);
	zassert_equal(string_1.value_num, strlen(string_1.value) + 1);
	zassert_equal(string_2.value_num, strlen(string_2.value) + 1);
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

ZTEST_SUITE(kv_store, NULL, kv_setup, kv_before, NULL, NULL);
