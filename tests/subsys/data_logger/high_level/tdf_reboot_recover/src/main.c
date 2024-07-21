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

#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/reboot.h>

struct tdf_buffer_state {
	/* Current buffer time */
	uint64_t time;
	/* Buffer information */
	struct net_buf_simple buf;
};
struct logger_data {
	uint32_t guard_head;
	struct k_sem lock;
	struct tdf_buffer_state tdf_state;
	uint8_t block_overhead;
	uint8_t tdf_buffer[DATA_LOGGER_MAX_SIZE(DT_NODELABEL(data_logger_epacket))];
	uint32_t guard_tail;
};

/* Not all bytes will be detected, only the important ones */
int corrupt_indicies[] = {
	offsetof(struct logger_data, guard_head),
	offsetof(struct logger_data, guard_tail),
	offsetof(struct logger_data, tdf_state.time),
	offsetof(struct logger_data, tdf_state.buf.data),
	offsetof(struct logger_data, tdf_state.buf.len),
	offsetof(struct logger_data, tdf_state.buf.size),
	offsetof(struct logger_data, tdf_state.buf.__buf),
	offsetof(struct logger_data, block_overhead),
	/* Length field of first TDF */
	offsetof(struct logger_data, tdf_buffer) + 2,
	/* Timestamp field of first TDF*/
	offsetof(struct logger_data, tdf_buffer) + 5,
};

static void log_corrupt_and_reboot(const struct device *tdf_logger, int corrupt_index)
{
	uint64_t t = 1000000;
	uint32_t data = 123;

	/* Push two TDFs */
	tdf_data_logger_log_dev(tdf_logger, 10, sizeof(data), t, &data);
	tdf_data_logger_log_dev(tdf_logger, 10, sizeof(data), t, &data);

	/* Corrupt the data struct if requested */
	if (corrupt_index >= 0) {
		uint8_t *data = tdf_logger->data;

		data[corrupt_index]++;
	}

	/* Reboot */
	infuse_reboot(INFUSE_REBOOT_UNKNOWN, 0, 0);
}

ZTEST(tdf_data_logger_recovery, test_logger_recovery)
{
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots;
	const struct device *tdf_logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_epacket));
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	struct net_buf *buf;
	int rc;

	zassert_not_null(sent_queue);

	/* KV store should have been initialised and populated with a reboot count */
	rc = KV_STORE_READ(KV_KEY_REBOOTS, &reboots);
	zassert_equal(sizeof(reboots), rc);

	if (reboots.count == 1) {
		/* First boot, should be no data recovered */
		zassert_equal(0, tdf_data_logger_flush_dev(tdf_logger));
		zassert_is_null(net_buf_get(sent_queue, K_MSEC(100)));

		/* Log TDFs and reboot */
		log_corrupt_and_reboot(tdf_logger, -1);
		zassert_unreachable();
	} else if (reboots.count == 2) {
		/* If we flush now, we should get the 2 TDFs we logged on the previous boot */
		zassert_equal(0, tdf_data_logger_flush_dev(tdf_logger));
		buf = net_buf_get(sent_queue, K_MSEC(100));
		zassert_not_null(buf);
		zassert_equal(sizeof(struct epacket_dummy_frame) + 22, buf->len);
		net_buf_unref(buf);

		void tdf_data_logger_lock(const struct device *dev);

		tdf_data_logger_log_dev(tdf_logger, 10, sizeof(*buf), 0, buf);
		tdf_data_logger_lock(tdf_logger);
		infuse_reboot(INFUSE_REBOOT_UNKNOWN, 0, 0);
		zassert_unreachable();
	} else if (reboots.count == 3) {
		/* If lock was taken over reboot data should be purged */
		zassert_equal(0, tdf_data_logger_flush_dev(tdf_logger));
		zassert_is_null(net_buf_get(sent_queue, K_MSEC(100)));

		/* Corrupt header guard */
		log_corrupt_and_reboot(tdf_logger, 0);
		zassert_unreachable();
	} else if (reboots.count < (ARRAY_SIZE(corrupt_indicies) + 3)) {
		/* Corrupted data should be detected and purged */
		zassert_equal(0, tdf_data_logger_flush_dev(tdf_logger));
		zassert_is_null(net_buf_get(sent_queue, K_MSEC(100)));

		log_corrupt_and_reboot(tdf_logger, corrupt_indicies[reboots.count - 4]);
		zassert_unreachable();
	}
}

ZTEST_SUITE(tdf_data_logger_recovery, NULL, NULL, NULL, NULL, NULL);
