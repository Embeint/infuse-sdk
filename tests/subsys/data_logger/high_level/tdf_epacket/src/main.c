/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdint.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>

#include <infuse/epacket/packet.h>
#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/tdf/tdf.h>

enum {
	TDF_RANDOM = 37,
};

void epacket_interface_common_init(const struct device *dev);
int logger_epacket_init(const struct device *dev);
int tdf_data_logger_init(const struct device *dev);

ZTEST(tdf_data_logger, test_log_error)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_epacket));
	uint8_t tdf_data[128];

	/* TDF's too large to fit on the log */
	for (int i = 62; i <= 70; i++) {
		zassert_equal(-ENOSPC, tdf_data_logger_log_dev(logger, TDF_RANDOM, i, 0, tdf_data));
	}
}

ZTEST(tdf_data_logger, test_standard)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_epacket));
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	uint8_t tdf_data[16];
	struct net_buf *buf;
	uint32_t init_size;
	int rc;

	init_size = tdf_data_logger_block_bytes_remaining(logger);
	zassert_equal(0, tdf_data_logger_block_bytes_pending(logger));
	zassert_not_equal(0, init_size);

	/* 7 bytes per log (3 overhead, 4 data) = 56 bytes */
	for (int i = 0; i < 8; i++) {
		rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, 4, 0, tdf_data);
		zassert_equal(0, rc);
	}
	zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));

	zassert_equal(56, tdf_data_logger_block_bytes_pending(logger));
	zassert_equal(init_size - 56, tdf_data_logger_block_bytes_remaining(logger));

	/* Flush logger */
	rc = tdf_data_logger_flush_dev(logger);
	zassert_equal(0, rc);

	zassert_equal(0, tdf_data_logger_block_bytes_pending(logger));
	zassert_equal(init_size, tdf_data_logger_block_bytes_remaining(logger));

	/* Validate payload sent */
	buf = k_fifo_get(sent_queue, K_MSEC(1));
	zassert_not_null(buf);
	zassert_equal(sizeof(struct epacket_dummy_frame) + 56, buf->len);
	net_buf_unref(buf);
}

ZTEST(tdf_data_logger, test_multi)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_epacket));
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	uint8_t tdf_data[128];
	struct net_buf *buf;
	int rc;

	/* 54 bytes (6 overhead, 12 * 4 data) */
	rc = tdf_data_logger_log_array_dev(logger, TDF_RANDOM, 4, 12, 0, 0, tdf_data);
	zassert_equal(0, rc);
	zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));

	/* Flush logger */
	rc = tdf_data_logger_flush_dev(logger);
	zassert_equal(0, rc);

	/* Validate payload sent */
	buf = k_fifo_get(sent_queue, K_MSEC(1));
	zassert_not_null(buf);
	zassert_equal(sizeof(struct epacket_dummy_frame) + 54, buf->len);
	net_buf_unref(buf);

	/* 42 bytes (6 overhead, 9 * 4 data) */
	rc = tdf_data_logger_log_array_dev(logger, TDF_RANDOM, 4, 9, 0, 0, tdf_data);
	zassert_equal(0, rc);
	zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));

	/* 42 bytes (6 overhead, 9 * 4 data) */
	rc = tdf_data_logger_log_array_dev(logger, TDF_RANDOM, 4, 9, 0, 0, tdf_data);
	zassert_equal(0, rc);

	/* First packet should have had the first call and 4 TDFs from the second */
	buf = k_fifo_get(sent_queue, K_MSEC(1));
	zassert_not_null(buf);
	zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));
	zassert_equal(sizeof(struct epacket_dummy_frame) + 64, buf->len);
	net_buf_unref(buf);

	/* Second packet should have the remaining 5 TDFs */
	rc = tdf_data_logger_flush_dev(logger);
	zassert_equal(0, rc);
	buf = k_fifo_get(sent_queue, K_MSEC(1));
	zassert_not_null(buf);
	zassert_equal(sizeof(struct epacket_dummy_frame) + 26, buf->len);
	net_buf_unref(buf);
}

ZTEST(tdf_data_logger, test_index_rollover)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_epacket));
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	uint8_t tdf_data[128];
	struct net_buf *buf;
	uint64_t time = 100000000;
	int idx = 0;
	int rc;

	/* 44 bytes (12 overhead, 8 * 4 data) */
	rc = tdf_data_logger_log_core_dev(logger, TDF_RANDOM, 4, 8, TDF_DATA_FORMAT_IDX_ARRAY, time,
					  idx, tdf_data);
	zassert_equal(0, rc);
	zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));
	idx += 8;

	/* 38 bytes (6 overhead, 8 * 4 data), should be split across blocks */
	time = 0;
	rc = tdf_data_logger_log_core_dev(logger, TDF_RANDOM, 4, 8, TDF_DATA_FORMAT_IDX_ARRAY, time,
					  idx, tdf_data);
	zassert_equal(0, rc);

	/* We expect 3 separate chunks logged across the two buffers.
	 * Only the first one should have a timestamp, but indicies should be consistently
	 * increasing.
	 */
	struct tdf_buffer_state state;
	struct tdf_parsed parsed;
	int cnt = 0;

	buf = k_fifo_get(sent_queue, K_MSEC(1));
	zassert_not_null(buf);
	net_buf_pull(buf, sizeof(struct epacket_dummy_frame));

	tdf_parse_start(&state, buf->data, buf->len);
	rc = tdf_parse(&state, &parsed);
	zassert_equal(0, rc);
	zassert_equal(TDF_RANDOM, parsed.tdf_id);
	zassert_equal(TDF_DATA_FORMAT_IDX_ARRAY, parsed.data_type);
	zassert_equal(100000000, parsed.time);
	zassert_equal(4, parsed.tdf_len);
	zassert_equal(8, parsed.tdf_num);
	zassert_equal(cnt, parsed.base_idx);
	cnt += parsed.tdf_num;

	rc = tdf_parse(&state, &parsed);
	zassert_equal(0, rc);
	zassert_equal(TDF_RANDOM, parsed.tdf_id);
	zassert_equal(TDF_DATA_FORMAT_IDX_ARRAY, parsed.data_type);
	zassert_equal(0, parsed.time);
	zassert_equal(4, parsed.tdf_len);
	zassert_equal(cnt, parsed.base_idx);
	cnt += parsed.tdf_num;

	rc = tdf_parse(&state, &parsed);
	zassert_equal(-ENOMEM, rc);
	net_buf_unref(buf);

	/* Second packet should have the remaining TDFs */
	rc = tdf_data_logger_flush_dev(logger);
	zassert_equal(0, rc);
	buf = k_fifo_get(sent_queue, K_MSEC(1));
	zassert_not_null(buf);
	net_buf_pull(buf, sizeof(struct epacket_dummy_frame));

	tdf_parse_start(&state, buf->data, buf->len);
	rc = tdf_parse(&state, &parsed);
	zassert_equal(0, rc);
	zassert_equal(TDF_RANDOM, parsed.tdf_id);
	zassert_equal(TDF_DATA_FORMAT_IDX_ARRAY, parsed.data_type);
	zassert_equal(0, parsed.time);
	zassert_equal(4, parsed.tdf_len);
	zassert_equal(cnt, parsed.base_idx);
	cnt += parsed.tdf_num;

	net_buf_unref(buf);

	rc = tdf_data_logger_flush_dev(logger);
	zassert_equal(0, rc);
	buf = k_fifo_get(sent_queue, K_MSEC(1));
	zassert_is_null(buf);

	/* Expected total number of readings */
	zassert_equal(16, cnt);
}

ZTEST(tdf_data_logger, test_index_time_rollover_reset)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_epacket));
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	uint8_t tdf_data[128];
	struct net_buf *buf;
	uint64_t time = 100000000;
	int rc;

	/* 92 bytes (12 overhead, 20 * 4 data) */
	rc = tdf_data_logger_log_core_dev(logger, TDF_RANDOM, 4, 20, TDF_DATA_FORMAT_IDX_ARRAY,
					  time, 0, tdf_data);
	zassert_equal(0, rc);
	buf = k_fifo_get(sent_queue, K_MSEC(1));
	zassert_not_null(buf);
	net_buf_pull(buf, sizeof(struct epacket_dummy_frame));

	struct tdf_buffer_state state;
	struct tdf_parsed parsed;
	int cnt = 0;

	/* First buffer has timestamp */
	tdf_parse_start(&state, buf->data, buf->len);
	rc = tdf_parse(&state, &parsed);
	zassert_equal(0, rc);
	zassert_equal(TDF_RANDOM, parsed.tdf_id);
	zassert_equal(TDF_DATA_FORMAT_IDX_ARRAY, parsed.data_type);
	zassert_equal(100000000, parsed.time);
	zassert_equal(4, parsed.tdf_len);
	zassert_equal(cnt, parsed.base_idx);
	cnt += parsed.tdf_num;
	net_buf_unref(buf);

	rc = tdf_data_logger_flush_dev(logger);
	zassert_equal(0, rc);
	buf = k_fifo_get(sent_queue, K_MSEC(1));
	zassert_not_null(buf);
	net_buf_pull(buf, sizeof(struct epacket_dummy_frame));

	/* Second buffer does not have the timestamp */
	tdf_parse_start(&state, buf->data, buf->len);
	rc = tdf_parse(&state, &parsed);
	zassert_equal(0, rc);
	zassert_equal(TDF_RANDOM, parsed.tdf_id);
	zassert_equal(TDF_DATA_FORMAT_IDX_ARRAY, parsed.data_type);
	zassert_equal(0, parsed.time);
	zassert_equal(4, parsed.tdf_len);
	zassert_equal(cnt, parsed.base_idx);
	cnt += parsed.tdf_num;

	net_buf_unref(buf);

	/* Cleanup */
	rc = tdf_data_logger_flush_dev(logger);
	zassert_equal(0, rc);
	buf = k_fifo_get(sent_queue, K_MSEC(1));
	zassert_is_null(buf);

	/* Expected total number of readings */
	zassert_equal(20, cnt);
}

ZTEST(tdf_data_logger, test_auto_flush)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_epacket));
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	uint8_t tdf_data[128];
	struct net_buf *buf;
	int rc;

	/* No data to start with */
	zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));

	/* 57 bytes should not flush */
	rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, 57, 0, tdf_data);
	zassert_equal(0, rc);
	zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));

	/* TDF of 2 bytes should flush */
	rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, 2, 0, tdf_data);
	zassert_equal(0, rc);
	buf = k_fifo_get(sent_queue, K_MSEC(1));
	zassert_not_null(buf);
	net_buf_unref(buf);

	/* Flush pending data */
	tdf_data_logger_flush_dev(logger);
	buf = k_fifo_get(sent_queue, K_MSEC(1));
	zassert_not_null(buf);
	net_buf_unref(buf);

	/* 58, 59, 60, 61 should auto flush */
	for (int i = 58; i <= 61; i++) {
		rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, i, 0, tdf_data);
		zassert_equal(0, rc);
		buf = k_fifo_get(sent_queue, K_MSEC(1));
		zassert_not_null(buf);
		net_buf_unref(buf);
	}
}

ZTEST(tdf_data_logger, test_size_change_decrease)
{
	const struct device *dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_epacket));
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	uint8_t tdf_data[128] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
	struct net_buf *buf;
	int rc;

	/* Log 32 bytes */
	rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, 29, 0, tdf_data);
	zassert_equal(0, rc);
	zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));

	/* Reducing the backend to 40 payload should not trigger any flush */
	epacket_dummy_set_max_packet(CONFIG_EPACKET_PACKET_SIZE_MAX - 24);
	epacket_dummy_set_interface_state(dummy, true);
	zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));

	/* But logging the next 8 bytes should flush */
	rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, 5, 0, tdf_data);
	zassert_equal(0, rc);
	buf = k_fifo_get(sent_queue, K_MSEC(1));
	zassert_not_null(buf);
	net_buf_unref(buf);

	/* Revert to full size */
	epacket_dummy_set_max_packet(CONFIG_EPACKET_PACKET_SIZE_MAX);
	epacket_dummy_set_interface_state(dummy, true);
	zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));

	/* Log 8 4 byte TDFs (56 bytes total) */
	for (int i = 0; i < 8; i++) {
		rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, 4, 0, tdf_data + i);
		zassert_equal(0, rc);
		zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));
	}

	/* Reduce backend to 24 bytes payload */
	epacket_dummy_set_max_packet(CONFIG_EPACKET_PACKET_SIZE_MAX - 40);
	epacket_dummy_set_interface_state(dummy, true);
	tdf_data_logger_flush_dev(logger);

	/* We expect 3 packets to be pending here, containing the 8 TDF's */
	struct tdf_buffer_state state;
	struct tdf_parsed parsed;
	int cnt = 0;

	for (int i = 0; i < 3; i++) {
		buf = k_fifo_get(sent_queue, K_MSEC(1));
		zassert_not_null(buf);
		net_buf_pull(buf, sizeof(struct epacket_dummy_frame));

		tdf_parse_start(&state, buf->data, buf->len);
		while (tdf_parse(&state, &parsed) == 0) {
			zassert_equal(TDF_RANDOM, parsed.tdf_id);
			zassert_equal(0, parsed.time);
			zassert_equal(1, parsed.tdf_num);
			zassert_equal(4, parsed.tdf_len);
			zassert_mem_equal(tdf_data + cnt, parsed.data, 4);
			cnt += 1;
		}
		net_buf_unref(buf);
	}
	zassert_equal(8, cnt);
	zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));
}

ZTEST(tdf_data_logger, test_diff_size_change_decrease)
{
	const struct device *dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_epacket));
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	uint16_t tdf_data[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
	struct net_buf *buf;
	int rc;

	epacket_dummy_set_max_packet(CONFIG_EPACKET_PACKET_SIZE_MAX);

	/* Log diff array, data size == (2 + 15) */
	rc = tdf_data_logger_log_core_dev(logger, TDF_RANDOM, sizeof(uint16_t),
					  ARRAY_SIZE(tdf_data), TDF_DATA_FORMAT_DIFF_ARRAY_16_8,
					  10000, 100, tdf_data);
	zassert_equal(0, rc);
	zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));

	/* Reducing the backend to 40 payload should not trigger any flush */
	epacket_dummy_set_max_packet(CONFIG_EPACKET_PACKET_SIZE_MAX - 28);
	epacket_dummy_set_interface_state(dummy, true);
	zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));

	/* Reducing the backend to 30 bytes will result in reparsing and flush */
	epacket_dummy_set_max_packet(CONFIG_EPACKET_PACKET_SIZE_MAX - 38);
	epacket_dummy_set_interface_state(dummy, true);

	buf = k_fifo_get(sent_queue, K_MSEC(1));
	zassert_not_null(buf);
	net_buf_pull(buf, sizeof(struct epacket_dummy_frame));

	struct tdf_buffer_state state;
	struct tdf_parsed parsed;
	uint16_t reconstructed;

	tdf_parse_start(&state, buf->data, buf->len);
	rc = tdf_parse(&state, &parsed);
	zassert_equal(0, rc);
	zassert_equal(TDF_RANDOM, parsed.tdf_id);
	zassert_equal(10000, parsed.time);
	zassert_equal(100, parsed.period);
	zassert_equal(2, parsed.tdf_len);
	zassert_equal(TDF_DATA_FORMAT_DIFF_ARRAY_16_8, parsed.data_type);
	/* There was not space for all the diffs */
	zassert_equal(12, parsed.diff_info.num);

	for (int i = 0; i < (1 + parsed.diff_info.num); i++) {
		rc = tdf_parse_diff_reconstruct(&parsed, &reconstructed, i);
		zassert_equal(0, rc);
		zassert_equal(i + 1, reconstructed);
	}
	net_buf_unref(buf);

	/* No more pending data */
	buf = k_fifo_get(sent_queue, K_MSEC(1));
	zassert_is_null(buf);
}

ZTEST(tdf_data_logger, test_size_change_increase)
{
	const struct device *dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_epacket));
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	uint8_t tdf_data[128];
	struct net_buf *buf;
	int rc;

	/* Start at 44 bytes (40 payload) */
	epacket_dummy_set_max_packet(44);
	epacket_dummy_set_interface_state(dummy, true);

	/* Log 32 bytes */
	rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, 30, 0, tdf_data);
	zassert_equal(0, rc);
	zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));

	/* Increase backend to full  */
	epacket_dummy_set_max_packet(CONFIG_EPACKET_PACKET_SIZE_MAX);
	epacket_dummy_set_interface_state(dummy, true);
	zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));

	/* Logging the next 8 bytes should not flush since backend is now larger */
	rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, 6, 0, tdf_data);
	zassert_equal(0, rc);
	zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));

	/* Logging to 64 byte payload will flush */
	rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, 22, 0, tdf_data);
	zassert_equal(0, rc);
	buf = k_fifo_get(sent_queue, K_MSEC(1));
	zassert_not_null(buf);
	net_buf_unref(buf);
}

ZTEST(tdf_data_logger, test_backend_disconnect)
{
	const struct device *dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_epacket));
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	uint8_t tdf_data[128];
	struct net_buf *buf;
	int rc;

	/* Log 32 bytes */
	rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, 30, 0, tdf_data);
	zassert_equal(0, rc);
	zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));

	/* Disconnect backend, nothing should be sent  */
	epacket_dummy_set_interface_state(dummy, false);
	zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));

	/* Log again, should be fine */
	rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, 10, 0, tdf_data);
	zassert_equal(0, rc);

	/* Reconnect backend, data should have been preserved */
	epacket_dummy_set_interface_state(dummy, true);
	tdf_data_logger_flush_dev(logger);
	buf = k_fifo_get(sent_queue, K_MSEC(1));
	zassert_not_null(buf);
	zassert_true(buf->len > (sizeof(struct epacket_dummy_frame) + 40));
	net_buf_unref(buf);

	/* Cycle with nothing pending */
	epacket_dummy_set_interface_state(dummy, false);
	epacket_dummy_set_interface_state(dummy, true);
	zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));

	/* Log and try to flush while disconnected */
	rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, 30, 0, tdf_data);
	zassert_equal(0, rc);
	zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));

	epacket_dummy_set_interface_state(dummy, false);
	rc = tdf_data_logger_flush_dev(logger);
	zassert_equal(-ENOTCONN, rc);
	epacket_dummy_set_interface_state(dummy, true);

	/* Data is lost here */
	rc = tdf_data_logger_flush_dev(logger);
	zassert_equal(0, rc);
	zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));

	/* Works as per usual here */
	rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, 30, 0, tdf_data);
	zassert_equal(0, rc);
	tdf_data_logger_flush_dev(logger);
	buf = k_fifo_get(sent_queue, K_MSEC(1));
	zassert_not_null(buf);
	net_buf_unref(buf);
}

ZTEST(tdf_data_logger, test_backend_disconnect_after_reboot)
{
	const struct device *dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	const struct device *data_logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_epacket));
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_epacket));
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	uint8_t tdf_data[128];
	struct net_buf *buf;
	int rc;

	/* Log 32 bytes */
	rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, 30, 0, tdf_data);
	zassert_equal(0, rc);
	zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));

	/* Pretend that backend boots in disconnected state */
	epacket_dummy_set_max_packet(0);
	epacket_interface_common_init(dummy);
	logger_epacket_init(data_logger);
	tdf_data_logger_init(logger);

	/* Even though we are disconnected, we should be able to continue filling the recovered
	 * buffer.
	 */
	rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, 10, 0, tdf_data);
	zassert_equal(0, rc);
	zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));

	/* Reconnect backend */
	epacket_dummy_set_max_packet(CONFIG_EPACKET_PACKET_SIZE_MAX);
	epacket_dummy_set_interface_state(dummy, true);

	/* Flushing the logger should have all the data */
	rc = tdf_data_logger_flush_dev(logger);
	zassert_equal(0, rc);
	buf = k_fifo_get(sent_queue, K_MSEC(1));
	zassert_not_null(buf);
	zassert_true(buf->len > (sizeof(struct epacket_dummy_frame) + 40));
	net_buf_unref(buf);
}

void data_logger_reset(void *fixture)
{
	const struct device *dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_epacket));
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	struct net_buf *buf;

	epacket_dummy_set_max_packet(CONFIG_EPACKET_PACKET_SIZE_MAX);
	epacket_dummy_set_interface_state(dummy, true);
	tdf_data_logger_flush_dev(logger);
	buf = k_fifo_get(sent_queue, K_MSEC(1));
	if (buf != NULL) {
		net_buf_unref(buf);
	}
}

ZTEST_SUITE(tdf_data_logger, NULL, NULL, data_logger_reset, NULL, NULL);
