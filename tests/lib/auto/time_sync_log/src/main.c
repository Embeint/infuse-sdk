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

#include <infuse/auto/time_sync_log.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/tdf/definitions.h>
#include <infuse/tdf/tdf.h>
#include <infuse/time/epoch.h>
#include <infuse/types.h>
#include <infuse/reboot.h>

int infuse_common_boot_last_reboot(struct infuse_reboot_state *state)
{
	memset(state, 0x00, sizeof(*state));
	state->reason = INFUSE_REBOOT_UNKNOWN;
	return 0;
}

static void expect_sync(struct k_fifo *sent_queue, uint8_t source, int32_t shift)
{
	struct epacket_dummy_frame *frame;
	struct tdf_buffer_state state;
	struct tdf_parsed parsed;
	struct tdf_time_sync *sync;
	struct net_buf *buf;

	buf = k_fifo_get(sent_queue, K_MSEC(1));
	zassert_not_null(buf);
	frame = (void *)buf->data;
	zassert_equal(INFUSE_TDF, frame->type);

	/* Single TDF of type TDF_TIME_SYNC */
	tdf_parse_start(&state, frame->payload, buf->len - sizeof(*frame));
	zassert_equal(0, tdf_parse(&state, &parsed));
	zassert_equal(TDF_TIME_SYNC, parsed.tdf_id);
	zassert_equal(1, parsed.tdf_num);
	sync = parsed.data;
	zassert_equal(source, sync->source);
	zassert_equal(shift, sync->shift);
	zassert_equal(-ENOMEM, tdf_parse(&state, &parsed));

	net_buf_unref(buf);
}

static void expect_reboot(struct k_fifo *sent_queue, bool expect)
{
	struct epacket_dummy_frame *frame;
	struct tdf_buffer_state state;
	struct tdf_parsed parsed;
	struct net_buf *buf;

	buf = k_fifo_get(sent_queue, K_MSEC(1));
	if (expect) {
		zassert_not_null(buf);
		frame = (void *)buf->data;
		zassert_equal(INFUSE_TDF, frame->type);

		/* Single TDF of type TDF_REBOOT_INFO*/
		tdf_parse_start(&state, frame->payload, buf->len - sizeof(*frame));
		zassert_equal(0, tdf_parse(&state, &parsed));
		zassert_equal(TDF_REBOOT_INFO, parsed.tdf_id);
		zassert_equal(1, parsed.tdf_num);
		zassert_equal(-ENOMEM, tdf_parse(&state, &parsed));
		net_buf_unref(buf);

	} else {
		zassert_is_null(buf);
	}
}

ZTEST(time_sync_log, test_auto_log)
{
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	struct timeutil_sync_instant reference = {
		.local = 10 * CONFIG_SYS_CLOCK_TICKS_PER_SEC,
		.ref = 100 * INFUSE_EPOCH_TIME_TICKS_PER_SEC,
	};

	zassert_not_null(sent_queue);

	/* Nothing should happen when reference set before configure */
	zassert_equal(0, epoch_time_set_reference(TIME_SOURCE_GNSS, &reference));
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));

	/* Configure automatic logging */
	auto_time_sync_log_configure(TDF_DATA_LOGGER_SERIAL, AUTO_TIME_SYNC_LOG_SYNCS);

	/* Jump forward in time, should see a TDF */
	reference.ref += INFUSE_EPOCH_TIME_TICKS_PER_SEC;
	zassert_equal(0, epoch_time_set_reference(TIME_SOURCE_GNSS, &reference));
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	expect_sync(sent_queue, TIME_SOURCE_GNSS, USEC_PER_SEC);

	/* Jump backwards in time, should see another TDF */
	reference.ref -= (3 * INFUSE_EPOCH_TIME_TICKS_PER_SEC) / 2;
	zassert_equal(0, epoch_time_set_reference(TIME_SOURCE_NTP, &reference));
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	expect_sync(sent_queue, TIME_SOURCE_NTP, (-3 * (int32_t)USEC_PER_SEC) / 2);
}

ZTEST(time_sync_log, test_reboot_log)
{
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	struct timeutil_sync_instant reference = {
		.local = 10 * CONFIG_SYS_CLOCK_TICKS_PER_SEC,
		.ref = 100 * INFUSE_EPOCH_TIME_TICKS_PER_SEC,
	};

	/* Configure automatic logging */
	auto_time_sync_log_configure(TDF_DATA_LOGGER_SERIAL, AUTO_TIME_SYNC_LOG_REBOOT_ON_SYNC);
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	expect_reboot(sent_queue, false);

	/* Update reference time, expect the REBOOT_INFO TDF */
	zassert_equal(0, epoch_time_set_reference(TIME_SOURCE_GNSS, &reference));
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	expect_reboot(sent_queue, true);

	/* Second update should not result in a log */
	reference.ref += INFUSE_EPOCH_TIME_TICKS_PER_SEC;
	zassert_equal(0, epoch_time_set_reference(TIME_SOURCE_GNSS, &reference));
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	expect_reboot(sent_queue, false);
}

ZTEST(time_sync_log, test_reboot_log_time_known)
{
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	struct timeutil_sync_instant reference = {
		.local = 10 * CONFIG_SYS_CLOCK_TICKS_PER_SEC,
		.ref = 100 * INFUSE_EPOCH_TIME_TICKS_PER_SEC,
	};

	/* Time recovered on boot */
	zassert_equal(
		0, epoch_time_set_reference(TIME_SOURCE_GNSS | TIME_SOURCE_RECOVERED, &reference));

	/* Configure automatic logging */
	auto_time_sync_log_configure(TDF_DATA_LOGGER_SERIAL, AUTO_TIME_SYNC_LOG_REBOOT_ON_SYNC);
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	expect_reboot(sent_queue, false);

	/* Update should not result in a log */
	reference.ref += INFUSE_EPOCH_TIME_TICKS_PER_SEC;
	zassert_equal(0, epoch_time_set_reference(TIME_SOURCE_GNSS, &reference));
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	expect_reboot(sent_queue, false);
}

void test_init(void *fixture)
{
	epoch_time_reset();
}

ZTEST_SUITE(time_sync_log, NULL, NULL, test_init, NULL, NULL);
