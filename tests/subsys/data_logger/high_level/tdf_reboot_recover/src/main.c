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
#include <zephyr/net_buf.h>

#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/reboot.h>
#include <infuse/tdf/tdf.h>
#include <infuse/tdf/util.h>

#define DT_DRV_COMPAT embeint_tdf_data_logger
#if DT_ANY_INST_HAS_BOOL_STATUS_OKAY(tdf_remote)
#define TDF_REMOTE_SUPPORT 1
#endif

struct test_tdf_buffer_state {
	/* Current buffer time */
	uint64_t time;
	/* Buffer information */
	struct net_buf_simple buf;
};

struct test_data_logger_cb {
	void (*block_size_update)(const struct device *dev, uint16_t block_size, void *user_data);
	void (*write_failure)(const struct device *dev, enum infuse_type data_type, const void *mem,
			      uint16_t mem_len, int reason, void *user_data);
	void *user_data;
	sys_snode_t node;
};

struct logger_data {
	uint32_t guard_head;
	struct k_sem lock;
	struct test_tdf_buffer_state tdf_state;
	struct test_data_logger_cb logger_cb;
#ifdef TDF_REMOTE_SUPPORT
	uint64_t remote_id;
#endif
	uint8_t full_block_write;
	uint8_t block_overhead;
	uint8_t tdf_buffer[DATA_LOGGER_MAX_SIZE(DT_NODELABEL(data_logger_dummy))] __aligned(4);
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
	offsetof(struct logger_data, full_block_write),
	offsetof(struct logger_data, block_overhead),
	/* Length field of first TDF */
	offsetof(struct logger_data, tdf_buffer) + 2,
	/* Timestamp field of first TDF*/
	offsetof(struct logger_data, tdf_buffer) + 5,
};

static void log_corrupt_and_reboot(const struct device *tdf_logger, int corrupt_index, bool fault)
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
	if (fault) {
		epoch_time_set_reference(TIME_SOURCE_NONE, NULL);
	} else {
		infuse_reboot(INFUSE_REBOOT_RPC, 0, 0);
	}
}

static void tdf_reboot_info_log_expect(uint8_t reason)
{
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_reboot_info *info;
	struct tdf_parsed tdf;
	struct net_buf *buf;

	tdf_reboot_info_log(TDF_DATA_LOGGER_SERIAL);
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);

	buf = k_fifo_get(sent_queue, K_MSEC(100));
	zassert_not_null(buf);
	net_buf_pull(buf, sizeof(struct epacket_dummy_frame));
	zassert_equal(0, tdf_parse_find_in_buf(buf->data, buf->len, TDF_REBOOT_INFO, &tdf));
	info = tdf.data;
	zassert_equal(reason, info->reason);
	if (reason == K_ERR_CPU_EXCEPTION) {
		/* Expect the full ESF to be logged */
		zassert_equal(0, tdf_parse_find_in_buf(buf->data, buf->len,
						       TDF_EXCEPTION_STACK_FRAME, &tdf));
	}
	net_buf_unref(buf);
}

ZTEST(tdf_data_logger_recovery, test_logger_recovery)
{
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots;
	const struct device *tdf_logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_dummy));
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	struct net_buf *buf;
	int rc;

#ifdef TDF_REMOTE_SUPPORT
	const struct device *tdf_remote_logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_remote));
#endif

	zassert_not_null(sent_queue);

	/* KV store should have been initialised and populated with a reboot count */
	rc = KV_STORE_READ(KV_KEY_REBOOTS, &reboots);
	zassert_equal(sizeof(reboots), rc);

	if (reboots.count == 1) {
		/* First boot, should be no data recovered */
		zassert_equal(0, tdf_data_logger_flush_dev(tdf_logger));
		zassert_is_null(k_fifo_get(sent_queue, K_MSEC(100)));

		/* Check we can log the reboot */
		tdf_reboot_info_log_expect(INFUSE_REBOOT_UNKNOWN);

		/* Log TDFs and reboot via a fault */
		log_corrupt_and_reboot(tdf_logger, -1, true);
		zassert_unreachable();
	} else if (reboots.count == 2) {
		/* If we flush now, we should get the 2 TDFs we logged on the previous boot */
		zassert_equal(0, tdf_data_logger_flush_dev(tdf_logger));
		buf = k_fifo_get(sent_queue, K_MSEC(100));
		zassert_not_null(buf);
		zassert_equal(sizeof(struct epacket_dummy_frame) + 22, buf->len);
		net_buf_unref(buf);

		/* Next reboot should detect the NULL dereference */
		tdf_reboot_info_log_expect(K_ERR_CPU_EXCEPTION);

		void tdf_data_logger_lock(const struct device *dev);

		tdf_data_logger_log_dev(tdf_logger, 10, sizeof(*buf), 0, buf);
		tdf_data_logger_lock(tdf_logger);
		infuse_reboot(INFUSE_REBOOT_UNKNOWN, 0, 0);
		zassert_unreachable();
	} else if (reboots.count == 3) {
		/* If lock was taken over reboot data should be purged */
		zassert_equal(0, tdf_data_logger_flush_dev(tdf_logger));
		zassert_is_null(k_fifo_get(sent_queue, K_MSEC(100)));

		/* Corrupt header guard */
		log_corrupt_and_reboot(tdf_logger, 0, false);
		zassert_unreachable();
	} else if (reboots.count < (ARRAY_SIZE(corrupt_indicies) + 3)) {
		/* Corrupted data should be detected and purged */
		zassert_equal(0, tdf_data_logger_flush_dev(tdf_logger));
		zassert_is_null(k_fifo_get(sent_queue, K_MSEC(100)));

		log_corrupt_and_reboot(tdf_logger, corrupt_indicies[reboots.count - 4], false);
		zassert_unreachable();
	}
#ifdef TDF_REMOTE_SUPPORT
	else if (reboots.count == (ARRAY_SIZE(corrupt_indicies) + 3)) {
		zassert_equal(0, tdf_data_logger_flush_dev(tdf_logger));
		zassert_is_null(k_fifo_get(sent_queue, K_MSEC(100)));

		tdf_data_logger_remote_id_set(tdf_remote_logger, 0x12345678);

		/* Log TDFs and reboot */
		log_corrupt_and_reboot(tdf_remote_logger, -1, false);
		zassert_unreachable();
	} else if (reboots.count == (ARRAY_SIZE(corrupt_indicies) + 4)) {
		/* If we flush now, we should get the 2 TDFs we logged on the previous boot */
		zassert_equal(0, tdf_data_logger_flush_dev(tdf_remote_logger));
		buf = k_fifo_get(sent_queue, K_MSEC(100));
		zassert_not_null(buf);
		zassert_equal(sizeof(struct epacket_dummy_frame) + 22 + sizeof(uint64_t), buf->len);
		net_buf_unref(buf);

		/* Log TDFs and reboot, corrupting the remote ID */
		tdf_data_logger_remote_id_set(tdf_remote_logger, 0x12345678);
		log_corrupt_and_reboot(tdf_remote_logger, offsetof(struct logger_data, remote_id),
				       false);
		zassert_unreachable();
	} else if (reboots.count < (2 * ARRAY_SIZE(corrupt_indicies))) {
		/* Corrupted data should be detected and purged */
		zassert_equal(0, tdf_data_logger_flush_dev(tdf_remote_logger));
		zassert_is_null(k_fifo_get(sent_queue, K_MSEC(100)));

		tdf_data_logger_remote_id_set(tdf_remote_logger, 0x12345678);
		log_corrupt_and_reboot(
			tdf_remote_logger,
			corrupt_indicies[reboots.count - ARRAY_SIZE(corrupt_indicies) - 4], false);
		zassert_unreachable();
	}
#endif /* TDF_REMOTE_SUPPORT */
}

ZTEST_SUITE(tdf_data_logger_recovery, NULL, NULL, NULL, NULL, NULL);
