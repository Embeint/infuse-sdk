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

#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/data_logger/logger.h>
#include <infuse/epacket/interface/epacket_dummy.h>

int logger_epacket_init(const struct device *dev);

ZTEST(data_logger_epacket, test_init_constants)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_epacket));
	struct data_logger_state state;

	data_logger_get_state(logger, &state);
	zassert_not_equal(0, state.block_size);
	zassert_equal(0, state.erase_unit);
	zassert_equal(0, state.block_overhead);
	zassert_equal(UINT32_MAX, state.physical_blocks);
	zassert_equal(UINT32_MAX, state.logical_blocks);
	zassert_false(state.requires_full_block_write);
}

ZTEST(data_logger_epacket, test_block_read)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_epacket));
	uint8_t buffer[16];

	/* Reading from ePacket should always fail */
	zassert_equal(-ENOTSUP, data_logger_block_read(logger, 0, 0, buffer, sizeof(buffer)));
	zassert_equal(-ENOTSUP, data_logger_block_read(logger, 10, 0, buffer, sizeof(buffer)));
	zassert_equal(-ENOTSUP,
		      data_logger_block_read(logger, UINT32_MAX, 0, buffer, sizeof(buffer)));
}

ZTEST(data_logger_epacket, test_block_write_error)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_epacket));
	uint8_t payload[EPACKET_INTERFACE_MAX_PAYLOAD(DT_NODELABEL(epacket_dummy)) + 1];
	int rc;

	/* Write random block */
	rc = data_logger_block_write(logger, 0, payload, sizeof(payload));
	zassert_equal(-EINVAL, rc);
}

ZTEST(data_logger_epacket, test_block_write)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_epacket));
	struct epacket_dummy_frame *frame;
	struct data_logger_state state;
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	struct net_buf *sent;
	uint8_t payload[EPACKET_INTERFACE_MAX_PAYLOAD(DT_NODELABEL(epacket_dummy))];
	int rc;

	for (int i = 0; i < 100; i++) {
		/* Write random block */
		rc = data_logger_block_write(logger, i, payload, sizeof(payload));
		zassert_equal(0, rc);

		/* Validate packet was sent */
		sent = net_buf_get(sent_queue, K_MSEC(1));
		zassert_not_null(sent);
		zassert_equal(sizeof(payload) + sizeof(struct epacket_dummy_frame), sent->len);
		frame = (void *)sent->data;
		zassert_equal(i, frame->type);
		zassert_equal(0, frame->flags);
		zassert_equal(EPACKET_AUTH_NETWORK, frame->auth);
		zassert_mem_equal(payload, frame->payload, sizeof(payload));
		net_buf_unref(sent);
		zassert_is_null(net_buf_get(sent_queue, K_NO_WAIT));

		data_logger_get_state(logger, &state);
		zassert_equal(i + 1, state.current_block);
	}

	/* Reinitialise */
	logger_epacket_init(logger);
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.current_block);
}

static void data_logger_setup(void *fixture)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_epacket));

	(void)logger_epacket_init(logger);
}

ZTEST_SUITE(data_logger_epacket, NULL, NULL, data_logger_setup, NULL, NULL);
