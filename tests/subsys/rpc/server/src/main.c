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

#include <infuse/types.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/rpc/types.h>

#include "../../../../../subsys/rpc/server.h"

ZTEST(rpc_server, test_drop_data)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct epacket_dummy_frame header = {0};
	struct infuse_rpc_data *data;
	uint8_t payload[16] = {0};

	data = (void *)payload;
	zassert_not_null(tx_fifo);

	/* Send data payloads without a command */
	for (int i = 0; i < 8; i++) {
		header.type = INFUSE_RPC_DATA;
		header.auth = EPACKET_AUTH_DEVICE;
		data->request_id = 0x12345678 + i;
		epacket_dummy_receive(epacket_dummy, &header, payload, sizeof(payload));
		k_sleep(K_MSEC(1));
	}
}

ZTEST(rpc_server, test_auth_failure)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct epacket_dummy_frame header = {0};
	struct infuse_rpc_req_header *req_header;
	uint8_t payload[16] = {0};

	req_header = (void *)payload;
	zassert_not_null(tx_fifo);

	/* Send an unauthorised command */
	header.type = INFUSE_RPC_CMD;
	header.auth = EPACKET_AUTH_FAILURE;
	req_header->command_id = RPC_BUILTIN_END;
	req_header->request_id = 0x12345678;
	epacket_dummy_receive(epacket_dummy, &header, payload, 16);

	/* No response */
	zassert_is_null(net_buf_get(tx_fifo, K_MSEC(100)));
}

ZTEST(rpc_server, test_invalid)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct epacket_dummy_frame *tx_header, header = {0};
	struct infuse_rpc_req_header *req_header;
	struct infuse_rpc_rsp_header *rsp_header;
	uint8_t payload[16] = {0};
	struct net_buf *tx;

	req_header = (void *)payload;
	zassert_not_null(tx_fifo);

	/* Send a command */
	header.type = INFUSE_RPC_CMD;
	header.auth = EPACKET_AUTH_NETWORK;
	req_header->command_id = RPC_BUILTIN_END;
	req_header->request_id = 0x12345678;
	epacket_dummy_receive(epacket_dummy, &header, payload, 16);

	tx = net_buf_get(tx_fifo, K_MSEC(100));
	zassert_not_null(tx);
	tx_header = (void *)tx->data;
	rsp_header = (void *)(tx->data + sizeof(*tx_header));
	zassert_equal(INFUSE_RPC_RSP, tx_header->type);
	zassert_equal(EPACKET_AUTH_NETWORK, tx_header->auth);
	zassert_equal(0x12345678, rsp_header->request_id);
	zassert_equal(RPC_BUILTIN_END, rsp_header->command_id);
	zassert_equal(-ENOTSUP, rsp_header->return_code);
	zassert_equal(sizeof(*tx_header) + sizeof(*rsp_header), tx->len);
	net_buf_unref(tx);

	zassert_is_null(net_buf_get(tx_fifo, K_MSEC(100)));
}

ZTEST(rpc_server, test_auth_level)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct epacket_dummy_frame *tx_header, header = {0};
	struct infuse_rpc_req_header *req_header;
	struct rpc_echo_response *rsp;
	uint32_t request_id = sys_rand32_get();
	uint8_t payload[16] = {0};
	struct net_buf *tx;

	req_header = (void *)payload;
	zassert_not_null(tx_fifo);

	header.type = INFUSE_RPC_CMD;
	req_header->request_id = request_id;

	/* ECHO with EPACKET_AUTH_DEVICE */
	req_header->command_id = RPC_ID_ECHO;
	header.auth = EPACKET_AUTH_DEVICE;
	epacket_dummy_receive(epacket_dummy, &header, payload,
			      sizeof(struct rpc_echo_request) + sizeof(payload));
	tx = net_buf_get(tx_fifo, K_MSEC(100));
	zassert_not_null(tx);
	tx_header = (void *)tx->data;
	rsp = (void *)(tx->data + sizeof(*tx_header));
	zassert_equal(INFUSE_RPC_RSP, tx_header->type);
	zassert_equal(EPACKET_AUTH_DEVICE, tx_header->auth);
	zassert_equal(0, rsp->header.return_code);
	net_buf_unref(tx);

	/* ECHO with EPACKET_AUTH_NETWORK */
	req_header->command_id = RPC_ID_ECHO;
	header.auth = EPACKET_AUTH_NETWORK;
	epacket_dummy_receive(epacket_dummy, &header, payload,
			      sizeof(struct rpc_echo_request) + sizeof(payload));
	tx = net_buf_get(tx_fifo, K_MSEC(100));
	zassert_not_null(tx);
	tx_header = (void *)tx->data;
	rsp = (void *)(tx->data + sizeof(*tx_header));
	zassert_equal(INFUSE_RPC_RSP, tx_header->type);
	zassert_equal(EPACKET_AUTH_NETWORK, tx_header->auth);
	zassert_equal(0, rsp->header.return_code);
	net_buf_unref(tx);

	/* DATA_SENDER with EPACKET_AUTH_NETWORK */
	req_header->command_id = RPC_ID_DATA_SENDER;
	header.auth = EPACKET_AUTH_NETWORK;
	epacket_dummy_receive(epacket_dummy, &header, payload,
			      sizeof(struct rpc_echo_request) + sizeof(payload));
	tx = net_buf_get(tx_fifo, K_MSEC(100));
	zassert_not_null(tx);
	tx_header = (void *)tx->data;
	rsp = (void *)(tx->data + sizeof(*tx_header));
	zassert_equal(INFUSE_RPC_RSP, tx_header->type);
	zassert_equal(EPACKET_AUTH_NETWORK, tx_header->auth);
	zassert_equal(-EACCES, rsp->header.return_code);
	net_buf_unref(tx);
}

ZTEST(rpc_server, test_echo_response)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct epacket_dummy_frame *tx_header, header = {0};
	struct infuse_rpc_req_header *req_header;
	struct rpc_echo_response *rsp;
	uint32_t request_id = sys_rand32_get();
	uint8_t payload[64] = {0};
	struct net_buf *tx;

	sys_rand_get(payload, sizeof(payload));
	req_header = (void *)payload;
	zassert_not_null(tx_fifo);

	const uint8_t lens[] = {4, 16, 32, 64};

	for (int i = 0; i < ARRAY_SIZE(lens); i++) {
		/* Send a command */
		header.type = INFUSE_RPC_CMD;
		header.auth = EPACKET_AUTH_DEVICE;
		req_header->command_id = RPC_ID_ECHO;
		req_header->request_id = request_id;

		epacket_dummy_receive(epacket_dummy, &header, payload,
				      sizeof(struct rpc_echo_request) + lens[i]);

		tx = net_buf_get(tx_fifo, K_MSEC(100));
		zassert_not_null(tx);
		tx_header = (void *)tx->data;
		rsp = (void *)(tx->data + sizeof(*tx_header));
		zassert_equal(INFUSE_RPC_RSP, tx_header->type);
		zassert_equal(EPACKET_AUTH_DEVICE, tx_header->auth);
		zassert_equal(request_id, rsp->header.request_id);
		zassert_equal(RPC_ID_ECHO, rsp->header.command_id);
		zassert_equal(0, rsp->header.return_code);
		zassert_equal(sizeof(*tx_header) + sizeof(*rsp) + lens[i], tx->len);

		net_buf_unref(tx);
	}
	zassert_is_null(net_buf_get(tx_fifo, K_MSEC(100)));
}

static void test_data_sender(uint32_t to_send)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct epacket_dummy_frame *tx_header, header = {0};
	struct rpc_data_sender_request *req;
	struct rpc_data_sender_response *rsp;
	struct infuse_rpc_data *data;
	uint8_t payload[64] = {0};
	struct net_buf *tx;
	bool receiving = true;
	uint32_t received, bytes_received = 0;
	uint32_t request_id = sys_rand32_get();
	uint32_t expected_offset = 0;

	req = (void *)payload;
	zassert_not_null(tx_fifo);

	/* Send a command */
	header.type = INFUSE_RPC_CMD;
	header.auth = EPACKET_AUTH_DEVICE;
	req->header.command_id = RPC_ID_DATA_SENDER;
	req->header.request_id = request_id;
	req->data_header.size = to_send;
	req->data_header.rx_ack_period = 0;

	epacket_dummy_receive(epacket_dummy, &header, payload,
			      sizeof(struct rpc_data_sender_request));

	while (receiving) {
		tx = net_buf_get(tx_fifo, K_MSEC(100));
		zassert_not_null(tx);
		tx_header = (void *)tx->data;
		zassert_equal(EPACKET_AUTH_DEVICE, tx_header->auth);

		if (tx_header->type == INFUSE_RPC_RSP) {
			receiving = false;
			rsp = (void *)(tx->data + sizeof(*tx_header));
			zassert_equal(request_id, rsp->header.request_id);
			zassert_equal(RPC_ID_DATA_SENDER, rsp->header.command_id);
			zassert_equal(0, rsp->header.return_code);
		} else if (tx_header->type == INFUSE_RPC_DATA) {
			data = (void *)(tx->data + sizeof(*tx_header));
			received = tx->len - sizeof(*tx_header) - sizeof(*data);
			zassert_true(received > 0);
			zassert_equal(request_id, data->request_id);
			zassert_equal(expected_offset, data->offset);
			bytes_received += received;
			expected_offset += received;
		} else {
			zassert_true(false, "Unexpected packet type");
		}

		net_buf_unref(tx);
	}
	zassert_is_null(net_buf_get(tx_fifo, K_MSEC(1)));
	zassert_equal(to_send, bytes_received);
}

ZTEST(rpc_server, test_data_sender)
{
	/* Various small payload sizes */
	for (int i = 0; i < CONFIG_EPACKET_PACKET_SIZE_MAX + 10; i++) {
		test_data_sender(i);
	}
	/* Several larger dumps */
	test_data_sender(1000);
	test_data_sender(5555);
	test_data_sender(33333);
}

static void test_data_receiver(uint32_t total_send, uint8_t skip_after, uint8_t stop_after,
			       uint8_t bad_id_after, uint8_t ack_period, bool too_much_data)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct epacket_dummy_frame *tx_header, header = {0};
	struct rpc_data_receiver_request *req;
	struct rpc_data_receiver_response *rsp;
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

	req = (void *)payload;
	zassert_not_null(tx_fifo);

	/* Send the initiating command */
	header.type = INFUSE_RPC_CMD;
	header.auth = EPACKET_AUTH_DEVICE;
	req->header.command_id = RPC_ID_DATA_RECEIVER;
	req->header.request_id = request_id;
	req->data_header.size = send_remaining;
	req->data_header.rx_ack_period = ack_period;
	epacket_dummy_receive(epacket_dummy, &header, payload,
			      sizeof(struct rpc_data_receiver_request));

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
				struct infuse_rpc_data_ack *data_ack;
				uint8_t num_offsets =
					(tx->len - sizeof(*tx_header) - sizeof(*data_ack)) /
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
	zassert_equal(EPACKET_AUTH_DEVICE, tx_header->auth);
	zassert_equal(request_id, rsp->header.request_id);
	zassert_equal(RPC_ID_DATA_RECEIVER, rsp->header.command_id);
	if (had_stop) {
		zassert_equal(-ETIMEDOUT, rsp->header.return_code);
	} else if (too_much_data) {
		zassert_equal(-EINVAL, rsp->header.return_code);
	} else {
		zassert_equal(0, rsp->header.return_code);
	}
	if (had_skip || had_stop || too_much_data) {
		zassert_true(total_send > rsp->received);
	} else {
		zassert_equal(total_send, rsp->received);
	}
	if (ack_period) {
		if (ack_period > RPC_SERVER_MAX_ACK_PERIOD) {
			zassert_equal(0, packets_acked);
		} else {
			zassert_true(packets_acked >= (packets_sent - ack_period - had_skip));
		}
	}

	net_buf_unref(tx);
}

ZTEST(rpc_server, test_data_receiver_sizes)
{
	/* Various data sizes */
	test_data_receiver(100, 0, 0, 0, 0, false);
	test_data_receiver(1000, 0, 0, 0, 0, false);
	test_data_receiver(3333, 0, 0, 0, 0, false);
}

ZTEST(rpc_server, test_data_receiver_lost_payload)
{
	/* "Lost" data payload after some packets */
	test_data_receiver(1000, 5, 0, 0, 0, false);
	test_data_receiver(1000, 10, 0, 0, 0, false);
}

ZTEST(rpc_server, test_data_receiver_early_hangup)
{
	/* Stop sending data after some packets */
	test_data_receiver(1000, 0, 3, 0, 0, false);
	test_data_receiver(1000, 0, 11, 0, 0, false);
}

ZTEST(rpc_server, test_data_receiver_invalid_request_id)
{
	/* Bad request ID after some packets */
	test_data_receiver(1000, 0, 0, 4, 0, false);
	test_data_receiver(1000, 0, 0, 10, 0, false);
}

ZTEST(rpc_server, test_data_receiver_data_ack)
{
	/* Generating INFUSE_DATA_ACK packets */
	test_data_receiver(1000, 0, 0, 0, 1, false);
	test_data_receiver(1000, 0, 0, 0, 2, false);
	test_data_receiver(1000, 0, 0, 0, 3, false);
	test_data_receiver(1000, 0, 0, 0, 4, false);
	test_data_receiver(1000, 0, 0, 0, RPC_SERVER_MAX_ACK_PERIOD, false);
	test_data_receiver(1000, 0, 0, 0, RPC_SERVER_MAX_ACK_PERIOD + 1, false);
}

ZTEST(rpc_server, test_data_receiver_everything_wrong)
{
	/* Everything going wrong */
	test_data_receiver(1000, 3, 0, 7, 1, false);
	test_data_receiver(1000, 3, 0, 7, 2, false);
}

ZTEST(rpc_server, test_data_receiver_push_too_much_data)
{
	/* Send too much data */
	test_data_receiver(1000, 0, 0, 0, 0, true);
}

ZTEST(rpc_server, test_data_ack_fn)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct epacket_dummy_frame *tx_header;
	struct infuse_rpc_data_ack *data_ack;
	uint32_t offsets[] = {3, 2, 6, 1, 7, 342, 343};
	uint32_t num_offsets;
	struct net_buf *tx;

	zassert_not_null(tx_fifo);

	for (int i = 0; i < ARRAY_SIZE(offsets); i++) {
		for (int j = 0; j <= i; j++) {
			rpc_server_ack_data(epacket_dummy, 0x1234, offsets[j], i + 1);
		}
		tx = net_buf_get(tx_fifo, K_MSEC(1));
		zassert_not_null(tx);
		tx_header = (void *)tx->data;
		data_ack = (void *)(tx->data + sizeof(*tx_header));
		num_offsets = (tx->len - sizeof(*tx_header) - sizeof(*data_ack)) / sizeof(uint32_t);

		/* Ensure DATA_ACK contains the sent offsets */
		zassert_equal(INFUSE_RPC_DATA_ACK, tx_header->type);
		zassert_equal(0x1234, data_ack->request_id);
		zassert_equal(i + 1, num_offsets);
		for (int j = 0; j < num_offsets; j++) {
			zassert_equal(offsets[j], data_ack->offsets[j]);
		}
		net_buf_unref(tx);

		zassert_is_null(net_buf_get(tx_fifo, K_MSEC(1)));
	}
}

ZTEST_SUITE(rpc_server, NULL, NULL, NULL, NULL, NULL);
