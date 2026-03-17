/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>

#include <infuse/auto/kv_update_log.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/tdf/definitions.h>
#include <infuse/tdf/tdf.h>
#include <infuse/time/epoch.h>
#include <infuse/types.h>

static void expect_data(struct k_fifo *sent_queue, uint16_t key, const void *data,
			uint16_t data_len)
{
	struct epacket_dummy_frame *frame;
	struct tdf_buffer_state state;
	struct tdf_parsed parsed;
	struct tdf_kvs_value_changed *changed;
	struct net_buf *buf;

	buf = k_fifo_get(sent_queue, K_MSEC(1));
	zassert_not_null(buf);
	frame = (void *)buf->data;
	zassert_equal(INFUSE_TDF, frame->type);

	/* Single TDF of type TDF_TIME_SYNC */
	tdf_parse_start(&state, frame->payload, buf->len - sizeof(*frame));
	zassert_equal(0, tdf_parse(&state, &parsed));
	zassert_equal(TDF_KVS_VALUE_CHANGED, parsed.tdf_id);
	zassert_equal(1, parsed.tdf_num);
	changed = parsed.data;
	zassert_equal(sizeof(uint16_t) + data_len, parsed.tdf_len);
	zassert_equal(changed->key, key);
	if (data_len > 0) {
		zassert_mem_equal(changed->value, data, data_len);
	}
	zassert_equal(-ENOMEM, tdf_parse(&state, &parsed));

	net_buf_unref(buf);
}

ZTEST(kv_update_log, test_kv_update_logs)
{
	KV_STRING_CONST(name1, "NAME1");
	KV_STRING_CONST(name2, "LONG NAME");
	KV_STRING_CONST(write_only, "SUPER SECRET");
	const char *write_only_result = "**************";
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	int rc;

	/* Clean startup state */
	(void)kv_store_delete(KV_KEY_DEVICE_NAME);
	(void)kv_store_delete(KV_KEY_WIFI_PSK);

	/* Configure automatic logging */
	auto_kv_update_log_configure(TDF_DATA_LOGGER_SERIAL);
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);

	/* Write a name, change should be logged */
	rc = kv_store_write(KV_KEY_DEVICE_NAME, &name1, sizeof(name1));
	zassert_equal(sizeof(name1), rc);
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	expect_data(sent_queue, KV_KEY_DEVICE_NAME, &name1, sizeof(name1));

	/* Write a different name, change should be logged */
	rc = kv_store_write(KV_KEY_DEVICE_NAME, &name2, sizeof(name2));
	zassert_equal(sizeof(name2), rc);
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	expect_data(sent_queue, KV_KEY_DEVICE_NAME, &name2, sizeof(name2));

	/* Delete the name, should be logged */
	rc = kv_store_delete(KV_KEY_DEVICE_NAME);
	zassert_equal(0, rc);
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	expect_data(sent_queue, KV_KEY_DEVICE_NAME, NULL, 0);

	/* Write to a write-only key, change logged, but contents obscured */
	rc = kv_store_write(KV_KEY_WIFI_PSK, &write_only, sizeof(write_only));
	zassert_equal(sizeof(write_only), rc);
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	expect_data(sent_queue, KV_KEY_WIFI_PSK, write_only_result, sizeof(write_only));

	/* Delete the write-only, should be logged */
	rc = kv_store_delete(KV_KEY_WIFI_PSK);
	zassert_equal(0, rc);
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	expect_data(sent_queue, KV_KEY_WIFI_PSK, NULL, 0);
}

void test_init(void *fixture)
{
	epoch_time_reset();
}

ZTEST_SUITE(kv_update_log, NULL, NULL, test_init, NULL, NULL);
