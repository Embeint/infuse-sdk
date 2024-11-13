/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <stdint.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/net/buf.h>

#include <infuse/epacket/packet.h>
#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/tdf/tdf.h>

enum {
	TDF_RANDOM = 37,
};

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
	int rc;

	/* 7 bytes per log (3 overhead, 4 data) = 56 bytes */
	for (int i = 0; i < 8; i++) {
		rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, 4, 0, tdf_data);
		zassert_equal(0, rc);
	}
	zassert_is_null(net_buf_get(sent_queue, K_MSEC(1)));

	/* Flush logger */
	rc = tdf_data_logger_flush_dev(logger);
	zassert_equal(0, rc);

	/* Validate payload sent */
	buf = net_buf_get(sent_queue, K_MSEC(1));
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
	zassert_is_null(net_buf_get(sent_queue, K_MSEC(1)));

	/* Flush logger */
	rc = tdf_data_logger_flush_dev(logger);
	zassert_equal(0, rc);

	/* Validate payload sent */
	buf = net_buf_get(sent_queue, K_MSEC(1));
	zassert_not_null(buf);
	zassert_equal(sizeof(struct epacket_dummy_frame) + 54, buf->len);
	net_buf_unref(buf);

	/* 42 bytes (6 overhead, 9 * 4 data) */
	rc = tdf_data_logger_log_array_dev(logger, TDF_RANDOM, 4, 9, 0, 0, tdf_data);
	zassert_equal(0, rc);
	zassert_is_null(net_buf_get(sent_queue, K_MSEC(1)));

	/* 42 bytes (6 overhead, 9 * 4 data) */
	rc = tdf_data_logger_log_array_dev(logger, TDF_RANDOM, 4, 9, 0, 0, tdf_data);
	zassert_equal(0, rc);

	/* First packet should have had the first call and 4 TDFs from the second */
	buf = net_buf_get(sent_queue, K_MSEC(1));
	zassert_not_null(buf);
	zassert_is_null(net_buf_get(sent_queue, K_MSEC(1)));
	zassert_equal(sizeof(struct epacket_dummy_frame) + 64, buf->len);
	net_buf_unref(buf);

	/* Second packet should have the remaining 5 TDFs */
	rc = tdf_data_logger_flush_dev(logger);
	zassert_equal(0, rc);
	buf = net_buf_get(sent_queue, K_MSEC(1));
	zassert_not_null(buf);
	zassert_equal(sizeof(struct epacket_dummy_frame) + 26, buf->len);
	net_buf_unref(buf);
}

ZTEST(tdf_data_logger, test_auto_flush)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_epacket));
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	uint8_t tdf_data[128];
	struct net_buf *buf;
	int rc;

	/* No data to start with */
	zassert_is_null(net_buf_get(sent_queue, K_MSEC(1)));

	/* 57 bytes should not flush */
	rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, 57, 0, tdf_data);
	zassert_equal(0, rc);
	zassert_is_null(net_buf_get(sent_queue, K_MSEC(1)));

	/* TDF of 2 bytes should flush */
	rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, 2, 0, tdf_data);
	zassert_equal(0, rc);
	buf = net_buf_get(sent_queue, K_MSEC(1));
	zassert_not_null(buf);
	net_buf_unref(buf);

	/* Flush pending data */
	tdf_data_logger_flush_dev(logger);
	buf = net_buf_get(sent_queue, K_MSEC(1));
	zassert_not_null(buf);
	net_buf_unref(buf);

	/* 58, 59, 60, 61 should auto flush */
	for (int i = 58; i <= 61; i++) {
		rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, i, 0, tdf_data);
		zassert_equal(0, rc);
		buf = net_buf_get(sent_queue, K_MSEC(1));
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
	zassert_is_null(net_buf_get(sent_queue, K_MSEC(1)));

	/* Reducing the backend to 40 payload should not trigger any flush */
	epacket_dummy_set_max_packet(CONFIG_EPACKET_PACKET_SIZE_MAX - 24);
	epacket_dummy_set_interface_state(dummy, true);
	zassert_is_null(net_buf_get(sent_queue, K_MSEC(1)));

	/* But logging the next 8 bytes should flush */
	rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, 5, 0, tdf_data);
	zassert_equal(0, rc);
	buf = net_buf_get(sent_queue, K_MSEC(1));
	zassert_not_null(buf);
	net_buf_unref(buf);

	/* Revert to full size */
	epacket_dummy_set_max_packet(CONFIG_EPACKET_PACKET_SIZE_MAX);
	epacket_dummy_set_interface_state(dummy, true);
	zassert_is_null(net_buf_get(sent_queue, K_MSEC(1)));

	/* Log 8 4 byte TDFs (56 bytes total) */
	for (int i = 0; i < 8; i++) {
		rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, 4, 0, tdf_data + i);
		zassert_equal(0, rc);
		zassert_is_null(net_buf_get(sent_queue, K_MSEC(1)));
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
		buf = net_buf_get(sent_queue, K_MSEC(1));
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
	zassert_is_null(net_buf_get(sent_queue, K_MSEC(1)));
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
	zassert_is_null(net_buf_get(sent_queue, K_MSEC(1)));

	/* Increase backend to full  */
	epacket_dummy_set_max_packet(CONFIG_EPACKET_PACKET_SIZE_MAX);
	epacket_dummy_set_interface_state(dummy, true);
	zassert_is_null(net_buf_get(sent_queue, K_MSEC(1)));

	/* Logging the next 8 bytes should not flush since backend is now larger */
	rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, 6, 0, tdf_data);
	zassert_equal(0, rc);
	zassert_is_null(net_buf_get(sent_queue, K_MSEC(1)));

	/* Logging to 64 byte payload will flush */
	rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, 22, 0, tdf_data);
	zassert_equal(0, rc);
	buf = net_buf_get(sent_queue, K_MSEC(1));
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
	zassert_is_null(net_buf_get(sent_queue, K_MSEC(1)));

	/* Disconnect backend, nothing should be sent  */
	epacket_dummy_set_interface_state(dummy, false);
	zassert_is_null(net_buf_get(sent_queue, K_MSEC(1)));

	/* Attempt to log, should error */
	rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, 30, 0, tdf_data);
	zassert_equal(-ENOTCONN, rc);

	/* Reconnect backend, nothing should be sent (previous data discarded) */
	epacket_dummy_set_interface_state(dummy, true);
	tdf_data_logger_flush_dev(logger);
	zassert_is_null(net_buf_get(sent_queue, K_MSEC(1)));

	/* Cycle with nothing pending */
	epacket_dummy_set_interface_state(dummy, false);
	epacket_dummy_set_interface_state(dummy, true);
	zassert_is_null(net_buf_get(sent_queue, K_MSEC(1)));

	/* Works as per usual here */
	rc = tdf_data_logger_log_dev(logger, TDF_RANDOM, 30, 0, tdf_data);
	zassert_equal(0, rc);
	tdf_data_logger_flush_dev(logger);
	buf = net_buf_get(sent_queue, K_MSEC(1));
	zassert_not_null(buf);
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
	buf = net_buf_get(sent_queue, K_MSEC(1));
	if (buf != NULL) {
		net_buf_unref(buf);
	}
}

ZTEST_SUITE(tdf_data_logger, NULL, NULL, data_logger_reset, NULL, NULL);
