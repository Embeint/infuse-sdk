/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>

#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

#include "../../../../../subsys/fs/kv_store/kv_internal.h"

#define STORAGE storage_partition

int kv_store_init(void);

ZTEST(kv_store, test_fs_get)
{
	zassert_not_null(kv_store_fs());
}

ZTEST(kv_store, test_init_failure)
{
	const struct device *dev = PARTITION_DEVICE(STORAGE);
	uint8_t zeroes[64] = {0x00};

	/* Write all the flash to 0 */
	for (int i = 0; i < PARTITION_SIZE(STORAGE); i += sizeof(zeroes)) {
		zassert_equal(
			0, flash_write(dev, PARTITION_OFFSET(STORAGE) + i, zeroes, sizeof(zeroes)));
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
		case KV_KEY_DEVICE_NAME:
		case KV_KEY_BOARD_TARGET:
		case KV_KEY_WIFI_PSK:
		case KV_KEY_LTE_SIM_UICC:
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

ZTEST(kv_store, test_key_write_only)
{
	/* Exhaustive check over every key */
	for (int i = 0; i <= UINT16_MAX; i++) {
		switch (i) {
		case KV_KEY_REBOOTS:
		case KV_KEY_DEVICE_NAME:
		case KV_KEY_BOARD_TARGET:
		case KV_KEY_LTE_SIM_UICC:
		case KV_KEY_GEOFENCE + 0:
		case KV_KEY_GEOFENCE + 1:
		case KV_KEY_GEOFENCE + 2:
		case KV_KEY_GEOFENCE + 3:
		case KV_KEY_GEOFENCE + 4:
			zassert_equal(0, kv_store_external_write_only(i));
			break;
		case KV_KEY_WIFI_PSK:
			zassert_equal(-EPERM, kv_store_external_write_only(i));
			break;
		default:
			zassert_equal(-EACCES, kv_store_external_write_only(i));
			break;
		}
	}
}

ZTEST(kv_store, test_key_read_only)
{
	/* Exhaustive check over every key */
	for (int i = 0; i <= UINT16_MAX; i++) {
		switch (i) {
		case KV_KEY_REBOOTS:
		case KV_KEY_DEVICE_NAME:
		case KV_KEY_WIFI_PSK:
		case KV_KEY_GEOFENCE + 0:
		case KV_KEY_GEOFENCE + 1:
		case KV_KEY_GEOFENCE + 2:
		case KV_KEY_GEOFENCE + 3:
		case KV_KEY_GEOFENCE + 4:
			zassert_equal(0, kv_store_external_read_only(i));
			break;
		case KV_KEY_BOARD_TARGET:
		case KV_KEY_LTE_SIM_UICC:
			zassert_equal(-EPERM, kv_store_external_read_only(i));
			break;
		default:
			zassert_equal(-EACCES, kv_store_external_write_only(i));
			break;
		}
	}
}

ZTEST(kv_store, test_metadata_reflect_idx)
{
	size_t idx;
	/* Exhaustive check over every key */
	for (int i = 0; i <= UINT16_MAX; i++) {
		kv_store_key_metadata(i, NULL, &idx);

		switch (i) {
		case KV_KEY_REBOOTS:
			zassert_equal(SIZE_MAX, idx);
			break;
		case KV_KEY_DEVICE_NAME:
			zassert_equal(0, idx);
			break;
		case KV_KEY_BOARD_TARGET:
			zassert_equal(1, idx);
			break;
		case KV_KEY_WIFI_PSK:
			zassert_equal(2, idx);
			break;
		case KV_KEY_LTE_SIM_UICC:
			zassert_equal(3, idx);
			break;
		case KV_KEY_GEOFENCE + 0:
			zassert_equal(4, idx);
			break;
		case KV_KEY_GEOFENCE + 1:
			zassert_equal(5, idx);
			break;
		case KV_KEY_GEOFENCE + 2:
			zassert_equal(6, idx);
			break;
		case KV_KEY_GEOFENCE + 3:
			zassert_equal(7, idx);
			break;
		case KV_KEY_GEOFENCE + 4:
			zassert_equal(8, idx);
			break;
		default:
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
	rc = kv_store_read_fallback(KV_KEY_FIXED_LOCATION, &location, sizeof(location), &fallback,
				    sizeof(fallback));
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

	/* Key does not exist */
	zassert_false(kv_store_key_exists(KV_KEY_REBOOTS));
	zassert_equal(-ENOENT, kv_store_key_data_size(KV_KEY_REBOOTS));

	/* Basic write, write duplicate, write new sequence */
	reboots.count = 10;
	rc = kv_store_write(KV_KEY_REBOOTS, &reboots, sizeof(reboots));
	zassert_equal(sizeof(reboots), rc);
	zassert_true(kv_store_key_exists(KV_KEY_REBOOTS));
	zassert_equal(sizeof(reboots), kv_store_key_data_size(KV_KEY_REBOOTS));
	rc = kv_store_write(KV_KEY_REBOOTS, &reboots, sizeof(reboots));
	zassert_equal(0, rc);
	reboots.count = 11;
	rc = kv_store_write(KV_KEY_REBOOTS, &reboots, sizeof(reboots));
	zassert_equal(sizeof(reboots), rc);
	zassert_true(kv_store_key_exists(KV_KEY_REBOOTS));
	zassert_equal(sizeof(reboots), kv_store_key_data_size(KV_KEY_REBOOTS));

	/* Validate written data */
	rc = kv_store_read(KV_KEY_REBOOTS, &reboots, sizeof(reboots));
	zassert_equal(sizeof(reboots), rc);
	zassert_equal(11, reboots.count);

	/* Delete and try to read */
	rc = kv_store_delete(KV_KEY_REBOOTS);
	zassert_equal(0, rc);
	zassert_false(kv_store_key_exists(KV_KEY_REBOOTS));
	zassert_equal(-ENOENT, kv_store_key_data_size(KV_KEY_REBOOTS));
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
	rc = kv_store_read_fallback(KV_KEY_REBOOTS, &reboots, sizeof(reboots), &fallback,
				    sizeof(fallback));
	zassert_equal(sizeof(reboots), rc);
	zassert_equal(100, reboots.count);

	/* Write new value */
	reboots.count += 10;
	rc = kv_store_write(KV_KEY_REBOOTS, &reboots, sizeof(reboots));
	zassert_equal(sizeof(reboots), rc);

	/* Second fallback read */
	rc = kv_store_read_fallback(KV_KEY_REBOOTS, &reboots, sizeof(reboots), &fallback,
				    sizeof(fallback));
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
	(void)kv_store_read_fallback(KV_KEY_REBOOTS, &reboots, sizeof(reboots), &fallback,
				     sizeof(fallback));
	zassert_equal(KV_KEY_REBOOTS, ctx.key);
	zassert_not_null(ctx.data);
	zassert_equal(sizeof(fallback), ctx.data_len);
	zassert_equal(3, ctx.cb_cnt);

	/* Unregistering prevents subsequent notifications */
	zassert_true(kv_store_unregister_callback(&cb));
	zassert_false(kv_store_unregister_callback(&cb));
	(void)kv_store_delete(KV_KEY_REBOOTS);
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

ZTEST(kv_store, test_kv_reflect_slots)
{
	struct key_value_slot_definition *defs;
	int expected = 0;
	size_t num;

	defs = kv_internal_slot_definitions(&num);
	for (int i = 0; i < num; i++) {
		if (defs[i].flags & KV_FLAGS_REFLECT) {
			expected += defs[i].range;
		}
	}
	zassert_not_equal(0, expected);
	zassert_equal(expected, KV_REFLECT_NUM);

	/* All slots should start at 0x00 */
	for (int i = 0; i < KV_REFLECT_NUM; i++) {
		zassert_equal(0x00, kv_reflect_key_crc(i));
	}
}

ZTEST(kv_store, test_kv_reflect_crc)
{
	KV_STRING_CONST(psk, "psk_string");
	KV_STRING_CONST(psk2, "psk_string_2");
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots = {.count = 42};
	KV_KEY_TYPE_VAR(KV_KEY_WIFI_PSK, 16) psk_read;
	uint32_t reflect_crc, prev_crc, initial_crc;

	/* Initial value */
	reflect_crc = kv_store_reflect_crc();
	initial_crc = reflect_crc;
	zassert_not_equal(0x00, reflect_crc);

	/* Write to key that is not reflected */
	prev_crc = reflect_crc;
	zassert_equal(sizeof(reboots), KV_STORE_WRITE(KV_KEY_REBOOTS, &reboots));
	reflect_crc = kv_store_reflect_crc();
	zassert_equal(prev_crc, reflect_crc);

	/* Write to key that is reflected */
	prev_crc = reflect_crc;
	zassert_equal(sizeof(psk), KV_STORE_WRITE(KV_KEY_WIFI_PSK, &psk));
	reflect_crc = kv_store_reflect_crc();
	zassert_not_equal(prev_crc, reflect_crc);

	/* Writing the same value again shouldn't change the CRC */
	prev_crc = reflect_crc;
	zassert_equal(0, KV_STORE_WRITE(KV_KEY_WIFI_PSK, &psk));
	zassert_equal(prev_crc, kv_store_reflect_crc());

	/* Re-inialising the KV store shouldn't change the CRC */
	zassert_equal(0, kv_store_init());
	zassert_equal(prev_crc, kv_store_reflect_crc());

	/* Changing the value should change the CRC */
	prev_crc = reflect_crc;
	zassert_equal(sizeof(psk2), KV_STORE_WRITE(KV_KEY_WIFI_PSK, &psk2));
	reflect_crc = kv_store_reflect_crc();
	zassert_not_equal(prev_crc, reflect_crc);

	/* Delete key that is reflected */
	prev_crc = reflect_crc;
	zassert_equal(0, kv_store_delete(KV_KEY_WIFI_PSK));
	reflect_crc = kv_store_reflect_crc();
	zassert_not_equal(prev_crc, reflect_crc);
	/* Should revert back to the initial CRC */
	zassert_equal(initial_crc, reflect_crc);

	/* Indirect write to key that is reflected */
	prev_crc = reflect_crc;
	KV_STORE_READ_FALLBACK(KV_KEY_WIFI_PSK, &psk_read, &psk);
	reflect_crc = kv_store_reflect_crc();
	zassert_not_equal(prev_crc, reflect_crc);

	/* Reset back */
	prev_crc = reflect_crc;
	zassert_equal(0, kv_store_delete(KV_KEY_WIFI_PSK));
	reflect_crc = kv_store_reflect_crc();
	zassert_not_equal(prev_crc, reflect_crc);
	/* Should revert back to the initial CRC */
	zassert_equal(initial_crc, reflect_crc);
}

ZTEST(kv_store, test_kv_reflect_order_invariant)
{
	KV_KEY_TYPE_VAR(KV_KEY_GEOFENCE, 2) geofence1 = {2, {{1, 2, 3}, {4, 5, 6}}};
	KV_KEY_TYPE_VAR(KV_KEY_GEOFENCE, 2) geofence2 = {2, {{7, 8, 9}, {1, 2, 3}}};
	KV_KEY_TYPE_VAR(KV_KEY_GEOFENCE, 2) geofence3 = {2, {{4, 5, 6}, {9, 8, 7}}};
	uint32_t initial_crc, final_crc;

	/* Initial value */
	initial_crc = kv_store_reflect_crc();
	zassert_not_equal(0x00, initial_crc);

	/* Write values in one order */
	zassert_equal(sizeof(geofence1), KV_STORE_WRITE(KV_KEY_GEOFENCE + 0, &geofence1));
	zassert_equal(sizeof(geofence2), KV_STORE_WRITE(KV_KEY_GEOFENCE + 1, &geofence2));
	zassert_equal(sizeof(geofence3), KV_STORE_WRITE(KV_KEY_GEOFENCE + 2, &geofence3));
	final_crc = kv_store_reflect_crc();
	zassert_not_equal(initial_crc, final_crc);

	/* Erase values */
	zassert_equal(0, kv_store_delete(KV_KEY_GEOFENCE + 0));
	zassert_equal(0, kv_store_delete(KV_KEY_GEOFENCE + 1));
	zassert_equal(0, kv_store_delete(KV_KEY_GEOFENCE + 2));
	zassert_equal(initial_crc, kv_store_reflect_crc());

	/* Write values in a different order, CRC should be the same */
	zassert_equal(sizeof(geofence3), KV_STORE_WRITE(KV_KEY_GEOFENCE + 2, &geofence3));
	zassert_equal(sizeof(geofence2), KV_STORE_WRITE(KV_KEY_GEOFENCE + 1, &geofence2));
	zassert_equal(sizeof(geofence1), KV_STORE_WRITE(KV_KEY_GEOFENCE + 0, &geofence1));
	zassert_equal(final_crc, kv_store_reflect_crc());

	/* Erase through resetting the store */
	zassert_equal(0, kv_store_reset());
	zassert_equal(initial_crc, kv_store_reflect_crc());
}

ZTEST(kv_store, test_short_buffer_read)
{
	KV_KEY_TYPE_VAR(KV_KEY_GEOFENCE, 4) geofence = {4, {{1, 2, 3}, {4, 5, 6}}};
	KV_KEY_TYPE_VAR(KV_KEY_GEOFENCE, 1) fallback = {1, {{7, 8, 9}}};
	uint8_t small[8];

	zassert_equal(sizeof(geofence), KV_STORE_WRITE(KV_KEY_GEOFENCE, &geofence));

	/* Full stored length returned, regardless of backend, with a partial copy */
	memset(small, 0x00, sizeof(small));
	zassert_equal(sizeof(geofence), kv_store_read(KV_KEY_GEOFENCE, small, sizeof(small)));
	zassert_mem_equal(&geofence, small, sizeof(small));

	/* Same behaviour through the fallback API when the value exists */
	memset(small, 0x00, sizeof(small));
	zassert_equal(sizeof(geofence),
		      kv_store_read_fallback(KV_KEY_GEOFENCE, small, sizeof(small), &fallback,
					     sizeof(fallback)));
	zassert_mem_equal(&geofence, small, sizeof(small));

	/* And when the fallback is written */
	memset(small, 0x00, sizeof(small));
	zassert_equal(sizeof(fallback),
		      kv_store_read_fallback(KV_KEY_GEOFENCE + 1, small, sizeof(small), &fallback,
					     sizeof(fallback)));
	zassert_mem_equal(&fallback, small, sizeof(small));
}

ZTEST(kv_store, test_zero_length)
{
	KV_KEY_TYPE_VAR(KV_KEY_GEOFENCE, 1) geofence = {1, {{1, 2, 3}}};
	uint32_t crc;

	zassert_equal(sizeof(geofence), KV_STORE_WRITE(KV_KEY_GEOFENCE, &geofence));
	crc = kv_store_reflect_crc();

	/* Zero length writes are rejected, value and reflection state unchanged */
	zassert_equal(-EINVAL, kv_store_write(KV_KEY_GEOFENCE, &geofence, 0));
	zassert_equal(sizeof(geofence), kv_store_key_data_size(KV_KEY_GEOFENCE));
	zassert_equal(crc, kv_store_reflect_crc());

	/* Disabled keys still report -EACCES */
	zassert_equal(-EACCES, kv_store_write(UINT16_MAX, &geofence, 0));

	/* Zero length fallback is rejected, nothing is written */
	zassert_equal(-EINVAL, kv_store_read_fallback(KV_KEY_GEOFENCE + 1, &geofence,
						      sizeof(geofence), &geofence, 0));
	zassert_false(kv_store_key_exists(KV_KEY_GEOFENCE + 1));
	zassert_equal(crc, kv_store_reflect_crc());
}

static int self_unregister_cnt;
static int trailing_cnt;
static struct kv_store_cb self_unregister_cb;

static void self_unregister_callback(uint16_t key, const void *data, size_t data_len,
				     void *user_ctx)
{
	self_unregister_cnt += 1;
	zassert_true(kv_store_unregister_callback(&self_unregister_cb));
}

static void trailing_callback(uint16_t key, const void *data, size_t data_len, void *user_ctx)
{
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots = {.count = 1234};

	trailing_cnt += 1;
	if (key == KV_KEY_GEOFENCE) {
		/* Writing from inside a callback must not deadlock */
		zassert_equal(sizeof(reboots), KV_STORE_WRITE(KV_KEY_REBOOTS, &reboots));
	}
}

ZTEST(kv_store, test_callback_reentrancy)
{
	static struct kv_store_cb trailing_cb = {.value_changed = trailing_callback};
	KV_KEY_TYPE_VAR(KV_KEY_GEOFENCE, 1) geofence = {1, {{1, 2, 3}}};
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots;

	self_unregister_cb.value_changed = self_unregister_callback;
	self_unregister_cnt = 0;
	trailing_cnt = 0;
	kv_store_register_callback(&self_unregister_cb);
	kv_store_register_callback(&trailing_cb);

	zassert_equal(sizeof(geofence), KV_STORE_WRITE(KV_KEY_GEOFENCE, &geofence));
	/* Self-unregistering callback ran once, later callback still ran for both writes */
	zassert_equal(1, self_unregister_cnt);
	zassert_equal(2, trailing_cnt);
	zassert_equal(sizeof(reboots), KV_STORE_READ(KV_KEY_REBOOTS, &reboots));
	zassert_equal(1234, reboots.count);

	zassert_true(kv_store_unregister_callback(&trailing_cb));
	zassert_false(kv_store_unregister_callback(&self_unregister_cb));
}

K_THREAD_STACK_DEFINE(writer_stack, 2048);
static struct k_thread writer_thread;
static atomic_t writer_done;
static int ordered_values[4];
static int ordered_num;

static KV_STRING_CONST(psk_first, "first_psk");
static KV_STRING_CONST(psk_second, "second_psk_value");

static void writer_fn(void *a, void *b, void *c)
{
	zassert_equal(sizeof(psk_second), KV_STORE_WRITE(KV_KEY_WIFI_PSK, &psk_second));
	atomic_set(&writer_done, 1);
}

static void ordering_callback(uint16_t key, const void *data, size_t data_len, void *user_ctx)
{
	if ((key != KV_KEY_WIFI_PSK) || (ordered_num >= ARRAY_SIZE(ordered_values))) {
		return;
	}
	ordered_values[ordered_num++] = data_len;
	if (data_len == sizeof(psk_first)) {
		/* Start a higher priority writer to the same key */
		k_thread_create(&writer_thread, writer_stack, K_THREAD_STACK_SIZEOF(writer_stack),
				writer_fn, NULL, NULL, NULL,
				k_thread_priority_get(k_current_get()) - 1, 0, K_NO_WAIT);
		/* Give the writer every opportunity to run (test thread may be cooperative) */
		k_sleep(K_MSEC(10));
		/* It must be blocked until this modification completes */
		zassert_equal(0, atomic_get(&writer_done));
	}
}

ZTEST(kv_store, test_modifications_serialised)
{
	static struct kv_store_cb cb = {.value_changed = ordering_callback};
	KV_KEY_TYPE_VAR(KV_KEY_WIFI_PSK, 32) psk;
	uint32_t crc;

	atomic_set(&writer_done, 0);
	ordered_num = 0;
	kv_store_register_callback(&cb);

	zassert_equal(sizeof(psk_first), KV_STORE_WRITE(KV_KEY_WIFI_PSK, &psk_first));
	zassert_equal(0, k_thread_join(&writer_thread, K_SECONDS(1)));
	zassert_equal(1, atomic_get(&writer_done));
	zassert_true(kv_store_unregister_callback(&cb));

	/* Callbacks delivered in write order, final value is the second write */
	zassert_equal(2, ordered_num);
	zassert_equal(sizeof(psk_first), ordered_values[0]);
	zassert_equal(sizeof(psk_second), ordered_values[1]);
	zassert_equal(sizeof(psk_second), KV_STORE_READ(KV_KEY_WIFI_PSK, &psk));
	zassert_mem_equal(&psk_second, &psk, sizeof(psk_second));

	/* Reflection state matches the stored value */
	crc = kv_store_reflect_crc();
	zassert_equal(0, kv_store_init());
	zassert_equal(crc, kv_store_reflect_crc());
}

ZTEST(kv_store, test_kv_reflect_max_size)
{
	KV_KEY_TYPE_VAR(KV_KEY_GEOFENCE, 2) small = {2, {{1, 2, 3}, {4, 5, 6}}};
	static uint8_t max[CONFIG_KV_STORE_REFLECT_MAX_SIZE];
	static uint8_t big[CONFIG_KV_STORE_REFLECT_MAX_SIZE + 1];
	uint32_t initial_crc, crc;

	initial_crc = kv_store_reflect_crc();
	max[0] = (sizeof(max) - 1) / sizeof(struct gcs_location);
	big[0] = (sizeof(big) - 1) / sizeof(struct gcs_location);

	/* Reflected values larger than the limit are rejected */
	zassert_equal(-EFBIG, kv_store_write(KV_KEY_GEOFENCE, big, sizeof(big)));
	zassert_false(kv_store_key_exists(KV_KEY_GEOFENCE));
	zassert_equal(-EFBIG, kv_store_read_fallback(KV_KEY_GEOFENCE, &small, sizeof(small), big,
						     sizeof(big)));
	zassert_false(kv_store_key_exists(KV_KEY_GEOFENCE));
	zassert_equal(initial_crc, kv_store_reflect_crc());

	/* Non-reflected keys are not limited */
	zassert_equal(sizeof(big), kv_store_write(KV_KEY_REBOOTS, big, sizeof(big)));

	/* Values at the limit are accepted, and the CRC is stable across init */
	zassert_equal(sizeof(max), kv_store_write(KV_KEY_GEOFENCE, max, sizeof(max)));
	crc = kv_store_reflect_crc();
	zassert_not_equal(initial_crc, crc);
	zassert_equal(0, kv_store_init());
	zassert_equal(crc, kv_store_reflect_crc());

	/* Modifying the final byte changes the CRC */
	max[sizeof(max) - 1] = 0xA5;
	zassert_equal(sizeof(max), kv_store_write(KV_KEY_GEOFENCE, max, sizeof(max)));
	zassert_not_equal(crc, kv_store_reflect_crc());
}

ZTEST(kv_store, test_kv_reflect_oversized_existing)
{
	static uint8_t big[CONFIG_KV_STORE_REFLECT_MAX_SIZE + 45];
	uint32_t len_le = sys_cpu_to_le32(sizeof(big));
	size_t reflect_idx;
	uint32_t crc;
	int rc;

	/* Value written before the limit was enforced, bypassing the KV store API */
#if defined(CONFIG_KV_STORE_NVS)
	rc = nvs_write(kv_store_fs(), KV_KEY_GEOFENCE, big, sizeof(big));
#else
	rc = zms_write(kv_store_fs(), ID_PRE | KV_KEY_GEOFENCE, big, sizeof(big));
#endif
	zassert_equal(sizeof(big), rc);

	/* Slot CRC is derived from the length, and stable across init */
	zassert_equal(0, kv_store_init());
	zassert_true(kv_store_key_metadata(KV_KEY_GEOFENCE, NULL, &reflect_idx));
	zassert_equal(crc32_ieee((void *)&len_le, sizeof(len_le)), kv_reflect_key_crc(reflect_idx));
	crc = kv_store_reflect_crc();
	zassert_equal(0, kv_store_init());
	zassert_equal(crc, kv_store_reflect_crc());

	/* Value can still be read and deleted */
	zassert_equal(sizeof(big), kv_store_key_data_size(KV_KEY_GEOFENCE));
	zassert_equal(0, kv_store_delete(KV_KEY_GEOFENCE));
	zassert_equal(0, kv_reflect_key_crc(reflect_idx));
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
