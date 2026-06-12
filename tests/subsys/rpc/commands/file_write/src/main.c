/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/pm/device_runtime.h>

#include <infuse/bluetooth/controller_manager.h>
#include <infuse/dfu/helpers.h>
#include <infuse/types.h>
#include <infuse/rpc/commands.h>
#include <infuse/rpc/server.h>
#include <infuse/rpc/types.h>
#include <infuse/security.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/littlefs.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/cpatch/patch.h>

static uint8_t fixed_payload[8192];
static uint32_t fixed_payload_crc;

struct test_out {
	int16_t cmd_rc;
	uint32_t cmd_crc;
	uint32_t cmd_len;
	uint32_t written_crc;
};
__maybe_unused static void zassert_device_released(const struct device *dev)
{
	int usage = pm_device_runtime_usage(dev);

	if ((usage == -ENOSYS) || (usage == -ENOTSUP)) {
		/* PM not supported/enabled */
		return;
	}
	/* Usage counter should be 0 */
	zassert_equal(0, usage);
}

static struct test_out test_file_write(uint8_t action, uint8_t folder, uint32_t filename,
				       uint32_t identifier, uint32_t total_send, uint8_t ack_period,
				       const void *fixed_source)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct epacket_dummy_frame *tx_header, header = {0};
	struct rpc_file_write_request *req;
	struct rpc_file_write_response *rsp;
	struct infuse_rpc_data_ack *data_ack;
	struct infuse_rpc_data *data_hdr;
	uint8_t payload[sizeof(struct infuse_rpc_data) + 64] = {0};
	uint32_t request_id = sys_rand32_get();
	uint32_t send_remaining = total_send;
	uint32_t tx_offset = 0;
	uint32_t to_send;
	struct net_buf *tx;
	uint32_t packets_acked = 0;
	uint32_t packets_sent = 0;
	uint32_t crc = UINT32_MAX;
	uint8_t num_offsets;

	req = (void *)payload;
	zassert_not_null(tx_fifo);

	if (fixed_source != NULL) {
		crc = crc32_ieee(fixed_source, total_send);
	}

	/* Send the initiating command */
	header.type = INFUSE_RPC_CMD;
	header.auth = EPACKET_AUTH_NETWORK;
	req->header.command_id = RPC_ID_FILE_WRITE;
	req->header.request_id = request_id;
	req->data_header.size = send_remaining;
	req->data_header.rx_ack_period = ack_period;
	req->action = action;
	req->file_crc = crc;
	req->folder = folder;
	req->filename = filename;
	req->identifier = identifier;

	epacket_dummy_receive(epacket_dummy, &header, payload, sizeof(*req));

	/* Expect an initial INFUSE_RPC_DATA_ACK to signify readiness */
	tx = k_fifo_get(tx_fifo, K_MSEC(100));
	tx_header = (void *)tx->data;
	data_ack = (void *)(tx->data + sizeof(*tx_header));
	num_offsets = (tx->len - sizeof(*tx_header) - sizeof(*data_ack)) / sizeof(uint32_t);
	if (tx_header->type == INFUSE_RPC_RSP) {
		goto early_rsp;
	}
	zassert_equal(INFUSE_RPC_DATA_ACK, tx_header->type);
	zassert_equal(EPACKET_AUTH_NETWORK, tx_header->auth);
	zassert_equal(request_id, data_ack->request_id);
	zassert_equal(0, num_offsets);
	net_buf_unref(tx);

	crc = 0;
	while (send_remaining > 0) {
		to_send = MIN(send_remaining, 64);
		data_hdr = (void *)payload;

		if (fixed_source != NULL) {
			/* Send fixed payload to the server */
			memcpy(data_hdr->payload, (const uint8_t *)fixed_source + tx_offset,
			       to_send);
		} else {
			/* Send randomised data to the server */
			sys_rand_get(payload, sizeof(payload));
		}

		header.type = INFUSE_RPC_DATA;
		data_hdr->request_id = request_id;
		data_hdr->offset = tx_offset;
		crc = crc32_ieee_update(crc, data_hdr->payload, to_send);

		/* Push payload over interface */
		packets_sent++;
		epacket_dummy_receive(epacket_dummy, &header, payload,
				      sizeof(struct infuse_rpc_data) + to_send);
		send_remaining -= to_send;
		tx_offset += to_send;
		tx = k_fifo_get(tx_fifo, K_NO_WAIT);
		if (tx) {
ack_handler:
			tx_header = (void *)tx->data;
			data_ack = (void *)(tx->data + sizeof(*tx_header));

			if (tx_header->type == INFUSE_RPC_RSP) {
				goto early_rsp;
			}
			zassert_equal(INFUSE_RPC_DATA_ACK, tx_header->type);

			num_offsets = (tx->len - sizeof(*tx_header) - sizeof(*data_ack)) /
				      sizeof(uint32_t);
			zassert_equal(ack_period, num_offsets);
			packets_acked += num_offsets;
			for (int i = 1; i < ack_period; i++) {
				zassert_true(data_ack->offsets[i - 1] < data_ack->offsets[i]);
			}
			net_buf_unref(tx);
		}
		k_sleep(K_MSEC(1));
	}

	/* Wait for the final RPC_RSP */
	tx = k_fifo_get(tx_fifo, K_MSEC(1000));
early_rsp:
	zassert_not_null(tx);
	tx_header = (void *)tx->data;
	if (ack_period && tx_header->type == INFUSE_RPC_DATA_ACK) {
		/* One last DATA_ACK packet, jump back to that handler */
		goto ack_handler;
	}
	rsp = (void *)(tx->data + sizeof(*tx_header));
	zassert_equal(INFUSE_RPC_RSP, tx_header->type);
	zassert_equal(EPACKET_AUTH_NETWORK, tx_header->auth);
	zassert_equal(RPC_ID_FILE_WRITE, rsp->header.command_id);
	zassert_equal(request_id, rsp->header.request_id);

	struct test_out ret = {
		.cmd_rc = rsp->header.return_code,
		.cmd_len = rsp->recv_len,
		.cmd_crc = rsp->recv_crc,
		.written_crc = crc,
	};

	net_buf_unref(tx);
	return ret;
}

ZTEST(rpc_command_file_write, test_file_write_for_copy_bad_folder)
{
	struct test_out ret;
	int rc;

	/* FILE_FOR_COPY must specify the FOR_COPY folder */
	ret = test_file_write(RPC_ENUM_FILE_ACTION_FILE_FOR_COPY, INFUSE_LFS_FOLDER_GENERAL, 10, 0,
			      100, 0, NULL);
	zassert_equal(-EINVAL, ret.cmd_rc);
	zassert_equal(0, ret.cmd_len);
	zassert_equal(0, ret.cmd_crc);

	/* No file created */
	rc = infuse_littlefs_file_size(INFUSE_LFS_FOLDER_GENERAL, 10);
	zassert_equal(-ENOENT, rc);
}

void *file_write_basic_setup(void)
{
	sys_rand_get(fixed_payload, sizeof(fixed_payload));
	fixed_payload_crc = crc32_ieee(fixed_payload, sizeof(fixed_payload));

#ifdef CONFIG_INFUSE_LITTLEFS
	(void)infuse_littlefs_init();
#endif /* CONFIG_INFUSE_LITTLEFS */
	return NULL;
}

ZTEST_SUITE(rpc_command_file_write, NULL, file_write_basic_setup, NULL, NULL, NULL);
