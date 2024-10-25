/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/ztest.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/storage/flash_map.h>

#include <infuse/types.h>
#include <infuse/rpc/types.h>
#include <infuse/security.h>
#include <infuse/fs/kv_store.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>

#include "../../../../../../subsys/rpc/server.h"

static void test_file_write_basic(uint8_t action, uint32_t total_send, uint8_t skip_after,
				  uint8_t stop_after, uint8_t bad_id_after, uint8_t ack_period,
				  bool too_much_data)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct epacket_dummy_frame *tx_header, header = {0};
	struct rpc_file_write_basic_request *req;
	struct rpc_file_write_basic_response *rsp;
	struct infuse_rpc_data_ack *data_ack;
	struct infuse_rpc_data *data_hdr;
	uint8_t payload[sizeof(struct infuse_rpc_data) + 64] = {0};
	uint32_t request_id = sys_rand32_get();
	uint32_t send_remaining = total_send;
	uint32_t tx_offset = 0;
	uint32_t to_send;
	struct net_buf *tx;
	bool had_skip = skip_after > 0;
	bool had_stop = stop_after > 0;
	uint32_t packets_acked = 0;
	uint32_t packets_sent = 0;
	uint32_t crc = 0;
	uint8_t num_offsets;

	req = (void *)payload;
	zassert_not_null(tx_fifo);

	/* Send the initiating command */
	header.type = INFUSE_RPC_CMD;
	header.auth = EPACKET_AUTH_NETWORK;
	req->header.command_id = RPC_ID_FILE_WRITE_BASIC;
	req->header.request_id = request_id;
	req->data_header.size = send_remaining;
	req->data_header.rx_ack_period = ack_period;
	req->action = action;
	epacket_dummy_receive(epacket_dummy, &header, payload,
			      sizeof(struct rpc_file_write_basic_request));

	/* Expect an initial INFUSE_RPC_DATA_ACK to signify readiness */
	tx = net_buf_get(tx_fifo, K_MSEC(100));
	tx_header = (void *)tx->data;
	data_ack = (void *)(tx->data + sizeof(*tx_header));
	num_offsets = (tx->len - sizeof(*tx_header) - sizeof(*data_ack)) / sizeof(uint32_t);
	zassert_equal(INFUSE_RPC_DATA_ACK, tx_header->type);
	zassert_equal(EPACKET_AUTH_NETWORK, tx_header->auth);
	zassert_equal(request_id, data_ack->request_id);
	zassert_equal(0, num_offsets);
	net_buf_unref(tx);

	while (send_remaining > 0) {
		/* Send randomised data to the server */
		sys_rand_get(payload, sizeof(payload));

		header.type = INFUSE_RPC_DATA;
		data_hdr = (void *)payload;
		data_hdr->request_id = request_id;
		data_hdr->offset = tx_offset;
		to_send = MIN(send_remaining, 64);

		/* Send a random packet with an invalid ID */
		if (bad_id_after && (bad_id_after-- == 1)) {
			data_hdr->request_id++;
			send_remaining += to_send;
			tx_offset -= to_send;
		} else {
			crc = crc32_ieee_update(crc, data_hdr->payload, to_send);
		}

		/* Push payload over interface */
		if (skip_after && (skip_after-- == 1)) {
			/* Skip packet */
		} else {
			packets_sent++;
			epacket_dummy_receive(epacket_dummy, &header, payload,
					      sizeof(struct infuse_rpc_data) +
						      (too_much_data ? 64 : to_send));
		}
		send_remaining -= to_send;
		tx_offset += to_send;
		if (stop_after && (stop_after-- == 1)) {
			break;
		}
		if (ack_period) {
			tx = net_buf_get(tx_fifo, K_NO_WAIT);
			if (tx) {
				/* clang-format off */
ack_handler:
				/* clang-format on */
				num_offsets = (tx->len - sizeof(*tx_header) - sizeof(*data_ack)) /
					      sizeof(uint32_t);

				tx_header = (void *)tx->data;
				data_ack = (void *)(tx->data + sizeof(*tx_header));
				zassert_equal(INFUSE_RPC_DATA_ACK, tx_header->type);
				zassert_equal(ack_period, num_offsets);
				packets_acked += num_offsets;
				for (int i = 1; i < ack_period; i++) {
					zassert_true(data_ack->offsets[i - 1] <
						     data_ack->offsets[i]);
				}
				net_buf_unref(tx);
			}
		}
		k_sleep(K_MSEC(1));
	}

	/* Wait for the final RPC_RSP */
	tx = net_buf_get(tx_fifo, K_MSEC(1000));
	zassert_not_null(tx);
	tx_header = (void *)tx->data;
	if (ack_period && tx_header->type == INFUSE_RPC_DATA_ACK) {
		/* One last DATA_ACK packet, jump back to that handler */
		goto ack_handler;
	}
	rsp = (void *)(tx->data + sizeof(*tx_header));
	zassert_equal(INFUSE_RPC_RSP, tx_header->type);
	zassert_equal(EPACKET_AUTH_NETWORK, tx_header->auth);
	zassert_equal(request_id, rsp->header.request_id);
	zassert_equal(RPC_ID_FILE_WRITE_BASIC, rsp->header.command_id);
	if (had_stop) {
		zassert_equal(-ETIMEDOUT, rsp->header.return_code);
	} else if (too_much_data) {
		zassert_equal(-EINVAL, rsp->header.return_code);
	} else {
		zassert_equal(0, rsp->header.return_code);
	}
	if (had_skip || had_stop || too_much_data) {
		zassert_true(total_send > rsp->recv_len);
	} else {
		zassert_equal(total_send, rsp->recv_len);
		zassert_equal(crc, rsp->recv_crc);
	}
	if (ack_period) {
		if (ack_period > RPC_SERVER_MAX_ACK_PERIOD) {
			zassert_equal(0, packets_acked);
		} else {
			zassert_true(packets_acked >= (packets_sent - ack_period - had_skip));
		}
	}

	net_buf_unref(tx);

	if (action != RPC_ENUM_FILE_ACTION_APP_IMG) {
		return;
	}

#if FIXED_PARTITION_EXISTS(slot1_partition)
	/* Validate file written matches flash contents */
	uint8_t buffer[128];
	const struct flash_area *fa;
	uint32_t fa_crc;

	zassert_equal(0, flash_area_open(FIXED_PARTITION_ID(slot1_partition), &fa));
	zassert_equal(0, flash_area_crc32(fa, 0, total_send, &fa_crc, buffer, sizeof(buffer)));
	zassert_equal(crc, fa_crc, "CRC sent does not equal CRC written");
	flash_area_close(fa);
#endif /* FIXED_PARTITION_EXISTS(slot1_partition) */
}

ZTEST(rpc_command_file_write_basic, test_invalid_action)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct epacket_dummy_frame hdr = {
		.auth = EPACKET_AUTH_NETWORK,
		.type = INFUSE_RPC_CMD,
	};
	struct rpc_file_write_basic_request req = {
		.header =
			{
				.command_id = RPC_ID_FILE_WRITE_BASIC,
				.request_id = sys_rand32_get(),
			},
		.data_header =
			{
				.size = 100,
				.rx_ack_period = 0,
			},
		.action = 200,
	};
	struct rpc_file_write_basic_response *rsp;
	struct epacket_dummy_frame *tx_header;
	struct net_buf *tx;

	epacket_dummy_receive(epacket_dummy, &hdr, &req, sizeof(req));

	/* Wait for the invalid response */
	tx = net_buf_get(tx_fifo, K_MSEC(1000));
	zassert_not_null(tx);
	tx_header = (void *)tx->data;
	rsp = (void *)(tx->data + sizeof(*tx_header));

	zassert_equal(INFUSE_RPC_RSP, tx_header->type);
	zassert_equal(-EINVAL, rsp->header.return_code);

	net_buf_unref(tx);
}

ZTEST(rpc_command_file_write_basic, test_file_write_sizes)
{
	/* Various data sizes */
	test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 100, 0, 0, 0, 0, false);
	test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 0, 0, 0, 0, false);
	test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 3333, 0, 0, 0, 0, false);
	/* Over UINT16_MAX */
	test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 100000, 0, 0, 0, 0, false);
}

ZTEST(rpc_command_file_write_basic, test_file_write_dfu)
{
#if FIXED_PARTITION_EXISTS(slot1_partition)
	test_file_write_basic(RPC_ENUM_FILE_ACTION_APP_IMG, 16000, 0, 0, 0, 0, false);
#endif /* FIXED_PARTITION_EXISTS(slot1_partition) */
}

ZTEST(rpc_command_file_write_basic, test_lost_payload)
{
	/* "Lost" data payload after some packets */
	test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 5, 0, 0, 0, false);
	test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 10, 0, 0, 0, false);
}

ZTEST(rpc_command_file_write_basic, test_early_hangup)
{
	/* Stop sending data after some packets */
	test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 0, 3, 0, 0, false);
	test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 0, 11, 0, 0, false);
}

ZTEST(rpc_command_file_write_basic, test_invalid_request_id)
{
	/* Bad request ID after some packets */
	test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 0, 0, 4, 0, false);
	test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 0, 0, 10, 0, false);
}

ZTEST(rpc_command_file_write_basic, test_data_ack)
{
	/* Generating INFUSE_DATA_ACK packets */
	test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 0, 0, 0, 1, false);
	test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 0, 0, 0, 2, false);
	test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 0, 0, 0, 3, false);
	test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 0, 0, 0, 4, false);
	test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 0, 0, 0,
			      RPC_SERVER_MAX_ACK_PERIOD, false);
	test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 0, 0, 0,
			      RPC_SERVER_MAX_ACK_PERIOD + 1, false);
}

ZTEST(rpc_command_file_write_basic, test_everything_wrong)
{
	/* Everything going wrong */
	test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 3, 0, 7, 1, false);
	test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 3, 0, 7, 2, false);
}

ZTEST(rpc_command_file_write_basic, test_push_too_much_data)
{
	/* Send too much data */
	test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 0, 0, 0, 0, true);
}

ZTEST_SUITE(rpc_command_file_write_basic, NULL, NULL, NULL, NULL, NULL);
