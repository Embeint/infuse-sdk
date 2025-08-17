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

#include <infuse/bluetooth/controller_manager.h>
#include <infuse/types.h>
#include <infuse/rpc/commands.h>
#include <infuse/rpc/server.h>
#include <infuse/rpc/types.h>
#include <infuse/security.h>
#include <infuse/fs/kv_store.h>
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

#ifdef CONFIG_TEST_NATIVE_MOCK

static size_t bt_image_len;
static size_t bt_image_crc;
static uint32_t bt_fail_after;
static bool bt_in_progress;
static int bt_start_rc;
static int bt_finish_rc;

int bt_controller_manager_file_write_start(uint32_t *ctx, uint8_t action, size_t image_len)
{
	bt_image_len = image_len;
	bt_image_crc = 0;
	if (bt_start_rc == 0) {
		bt_in_progress = true;
	}
	return bt_start_rc;
}

int bt_controller_manager_file_write_next(uint32_t ctx, uint32_t image_offset,
					  const void *image_chunk, size_t chunk_len)
{
	zassert_equal(0, image_offset % sizeof(uint32_t));

	bt_image_crc = crc32_ieee_update(bt_image_crc, image_chunk, chunk_len);

	if (bt_fail_after > 0) {
		if (--bt_fail_after == 0) {
			return -EIO;
		}
	}
	return 0;
}

int bt_controller_manager_file_write_finish(uint32_t ctx, uint32_t *len, uint32_t *crc)
{
	*len = bt_image_len;
	*crc = bt_image_crc;
	bt_in_progress = false;
	return bt_finish_rc;
}

#endif /* CONFIG_TEST_NATIVE_MOCK */

static struct test_out test_file_write_basic(uint8_t action, uint32_t total_send,
					     uint8_t skip_after, uint8_t stop_after,
					     uint8_t bad_id_after, uint8_t ack_period,
					     bool too_much_data, bool expect_skip,
					     uint8_t *fixed_source)
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
	uint32_t packets_acked = 0;
	uint32_t packets_sent = 0;
	uint32_t crc = UINT32_MAX;
	uint8_t num_offsets;

	req = (void *)payload;
	zassert_not_null(tx_fifo);

	if (fixed_source != NULL) {
		crc = crc32_ieee(fixed_payload, sizeof(fixed_payload));
	}

	/* Send the initiating command */
	header.type = INFUSE_RPC_CMD;
	header.auth = EPACKET_AUTH_NETWORK;
	req->header.command_id = RPC_ID_FILE_WRITE_BASIC;
	req->header.request_id = request_id;
	req->data_header.size = send_remaining;
	req->data_header.rx_ack_period = ack_period;
	req->action = action;
	req->file_crc = crc;
	epacket_dummy_receive(epacket_dummy, &header, payload,
			      sizeof(struct rpc_file_write_basic_request));

	if (expect_skip) {
		goto write_skip;
	}

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
			memcpy(data_hdr->payload, fixed_source + tx_offset, to_send);
		} else {
			/* Send randomised data to the server */
			sys_rand_get(payload, sizeof(payload));
		}

		header.type = INFUSE_RPC_DATA;
		data_hdr->request_id = request_id;
		data_hdr->offset = tx_offset;

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

write_skip:
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
	zassert_equal(request_id, rsp->header.request_id);
	zassert_equal(RPC_ID_FILE_WRITE_BASIC, rsp->header.command_id);

	struct test_out ret = {
		.cmd_rc = rsp->header.return_code,
		.cmd_len = rsp->recv_len,
		.cmd_crc = rsp->recv_crc,
		.written_crc = crc,
	};

	net_buf_unref(tx);
	return ret;
}

void validate_flash_area(struct test_out *ret, uint8_t partition_id)
{
#if FIXED_PARTITION_EXISTS(slot1_partition)
	/* Validate file written matches flash contents */
	uint8_t buffer[128];
	const struct flash_area *fa;
	uint32_t fa_crc;

	zassert_equal(0, flash_area_open(partition_id, &fa));
	zassert_equal(0, flash_area_crc32(fa, 0, ret->cmd_len, &fa_crc, buffer, sizeof(buffer)));
	zassert_equal(ret->cmd_crc, fa_crc, "CRC sent does not equal CRC written");
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
	tx = k_fifo_get(tx_fifo, K_MSEC(1000));
	zassert_not_null(tx);
	tx_header = (void *)tx->data;
	rsp = (void *)(tx->data + sizeof(*tx_header));

	zassert_equal(INFUSE_RPC_RSP, tx_header->type);
	zassert_equal(-EINVAL, rsp->header.return_code);

	net_buf_unref(tx);
}

ZTEST(rpc_command_file_write_basic, test_file_write_sizes)
{
	struct test_out ret;

	/* Various data sizes */
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 100, 0, 0, 0, 0, false, false,
				    NULL);
	zassert_equal(0, ret.cmd_rc);
	zassert_equal(100, ret.cmd_len);
	zassert_equal(ret.written_crc, ret.cmd_crc);
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 0, 0, 0, 0, false, false,
				    NULL);
	zassert_equal(0, ret.cmd_rc);
	zassert_equal(1000, ret.cmd_len);
	zassert_equal(ret.written_crc, ret.cmd_crc);
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 3333, 0, 0, 0, 0, false, false,
				    NULL);
	zassert_equal(0, ret.cmd_rc);
	zassert_equal(3333, ret.cmd_len);
	zassert_equal(ret.written_crc, ret.cmd_crc);
	/* Over UINT16_MAX */
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 100000, 0, 0, 0, 0, false, false,
				    NULL);
	zassert_equal(0, ret.cmd_rc);
	zassert_equal(100000, ret.cmd_len);
	zassert_equal(ret.written_crc, ret.cmd_crc);
}

#if FIXED_PARTITION_EXISTS(slot1_partition)
ZTEST(rpc_command_file_write_basic, test_file_write_dfu)
{
	struct test_out ret;

	/* Size aligned data payload */
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_APP_IMG, 16000, 0, 0, 0, 0, false, false,
				    NULL);
	zassert_equal(0, ret.cmd_rc);
	zassert_equal(16000, ret.cmd_len);
	zassert_equal(ret.written_crc, ret.cmd_crc);
	validate_flash_area(&ret, FIXED_PARTITION_ID(slot1_partition));
	/* Data payload with odd length */
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_APP_IMG, 16001, 0, 0, 0, 0, false, false,
				    NULL);
	zassert_equal(0, ret.cmd_rc);
	zassert_equal(16001, ret.cmd_len);
	zassert_equal(ret.written_crc, ret.cmd_crc);
	validate_flash_area(&ret, FIXED_PARTITION_ID(slot1_partition));
	/* Known payload twice, second should skip the write */
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_APP_IMG, sizeof(fixed_payload), 0, 0, 0, 0,
				    false, false, fixed_payload);
	zassert_equal(0, ret.cmd_rc);
	zassert_equal(sizeof(fixed_payload), ret.cmd_len);
	zassert_equal(fixed_payload_crc, ret.cmd_crc);
	validate_flash_area(&ret, FIXED_PARTITION_ID(slot1_partition));
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_APP_IMG, sizeof(fixed_payload), 0, 0, 0, 0,
				    false, true, fixed_payload);
	zassert_equal(0, ret.cmd_rc);
	zassert_equal(0, ret.cmd_len);
	zassert_equal(fixed_payload_crc, ret.cmd_crc);
	ret.cmd_len = sizeof(fixed_payload);
	validate_flash_area(&ret, FIXED_PARTITION_ID(slot1_partition));
}
#else
ZTEST(rpc_command_file_write_basic, test_file_write_dfu)
{
	ztest_test_skip();
}
#endif /* FIXED_PARTITION_EXISTS(slot1_partition) */

static void flash_area_copy(uint8_t partition_dst, uint8_t partition_src, uint32_t len,
			    bool source_erase)
{
	const struct flash_area *fa_dst, *fa_src;
	uint8_t buffer[128];
	uint32_t off = 0;

	zassert_equal(0, flash_area_open(partition_dst, &fa_dst));
	zassert_equal(0, flash_area_open(partition_src, &fa_src));

	zassert_equal(0, flash_area_erase(fa_dst, 0, fa_dst->fa_size));

	while (off < len) {
		zassert_equal(0, flash_area_read(fa_src, off, buffer, sizeof(buffer)));
		zassert_equal(0, flash_area_write(fa_dst, off, buffer, sizeof(buffer)));
		off += sizeof(buffer);
	}

	if (source_erase) {
		zassert_equal(0, flash_area_erase(fa_src, 0, fa_src->fa_size));
	}

	flash_area_close(fa_dst);
	flash_area_close(fa_src);
}

struct patch_file {
	struct cpatch_header header;
	uint8_t patch[5];
} __packed hardcoded_patch;

#if FIXED_PARTITION_EXISTS(file_partition)
ZTEST(rpc_command_file_write_basic, test_file_write_dfu_cpatch)
{
	struct test_out ret;

	/* Write an arbitrary image of known size to partition1 */
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_APP_IMG, 17023, 0, 0, 0, 0, false, false,
				    NULL);
	zassert_equal(0, ret.cmd_rc);
	zassert_equal(17023, ret.cmd_len);
	zassert_equal(ret.written_crc, ret.cmd_crc);
	validate_flash_area(&ret, FIXED_PARTITION_ID(slot1_partition));

	/* Copy the base image into partition0 and erase partition1 */
	flash_area_copy(FIXED_PARTITION_ID(slot0_partition), FIXED_PARTITION_ID(slot1_partition),
			17023, true);

	/* Construct patch file that just regenerates the original file */
	hardcoded_patch.patch[0] = 48; /* COPY_LEN_U32 */
	sys_put_le32(17023, hardcoded_patch.patch + 1);

	hardcoded_patch.header.magic_value = CPATCH_MAGIC_NUMBER;
	hardcoded_patch.header.version_major = 1;
	hardcoded_patch.header.version_minor = 0;
	hardcoded_patch.header.input_file.length = 17023;
	hardcoded_patch.header.input_file.crc = ret.written_crc;
	hardcoded_patch.header.output_file.length = 17023;
	hardcoded_patch.header.output_file.crc = ret.written_crc;
	hardcoded_patch.header.patch_file.length = 5;
	hardcoded_patch.header.patch_file.crc = crc32_ieee(hardcoded_patch.patch, 5);
	hardcoded_patch.header.header_crc =
		crc32_ieee((const void *)&hardcoded_patch.header,
			   sizeof(hardcoded_patch.header) - sizeof(uint32_t));

	/* Write the patch file */
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_APP_CPATCH, sizeof(hardcoded_patch), 0, 0,
				    0, 0, false, false, (void *)&hardcoded_patch);
	zassert_equal(0, ret.cmd_rc);
	zassert_equal(sizeof(hardcoded_patch), ret.cmd_len);
	zassert_equal(ret.written_crc, ret.cmd_crc);

	/* Give command a chance to finish writing the patch */
	k_sleep(K_MSEC(100));

	/* Validate partition1 now matches partition0 */
	uint8_t buffer[128];
	const struct flash_area *fa;
	uint32_t fa_crc;

	zassert_equal(0, flash_area_open(FIXED_PARTITION_ID(slot1_partition), &fa));
	zassert_equal(0, flash_area_crc32(fa, 0, hardcoded_patch.header.output_file.length, &fa_crc,
					  buffer, sizeof(buffer)));
	zassert_equal(hardcoded_patch.header.output_file.crc, fa_crc,
		      "CRC constructed does not equal original CRC");
	flash_area_close(fa);

	/* Corrupt the file sent */
	hardcoded_patch.patch[3] += 1;

	/* Write the patch file, validate failure */
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_APP_CPATCH, sizeof(hardcoded_patch), 0, 0,
				    0, 0, false, false, (void *)&hardcoded_patch);
	zassert_equal(-EINVAL, ret.cmd_rc);
	zassert_equal(sizeof(hardcoded_patch), ret.cmd_len);
}

#else

ZTEST(rpc_command_file_write_basic, test_file_write_dfu_cpatch)
{
	(void)flash_area_copy;

	ztest_test_skip();
}

#endif /* FIXED_PARTITION_EXISTS(file_partition) */

#if FIXED_PARTITION_EXISTS(file_partition)
ZTEST(rpc_command_file_write_basic, test_file_write_for_copy)
{
	struct test_out ret;

	/* Write an arbitrary image of known size to file_partition */
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_FILE_FOR_COPY, 17023, 0, 0, 0, 0, false,
				    false, NULL);
	zassert_equal(0, ret.cmd_rc);
	zassert_equal(17023, ret.cmd_len);
	zassert_equal(ret.written_crc, ret.cmd_crc);
	validate_flash_area(&ret, FIXED_PARTITION_ID(file_partition));
}
#else

ZTEST(rpc_command_file_write_basic, test_file_write_for_copy)
{
	(void)flash_area_copy;

	ztest_test_skip();
}

#endif /* FIXED_PARTITION_EXISTS(file_partition) */

ZTEST(rpc_command_file_write_basic, test_file_write_bt_ctlr)
{
#ifdef CONFIG_TEST_NATIVE_MOCK
	struct test_out ret;

	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_BT_CTLR_IMG, 6000, 0, 0, 0, 0, false,
				    false, NULL);
	zassert_equal(0, ret.cmd_rc);
	zassert_equal(6000, ret.cmd_len);
	zassert_equal(ret.written_crc, ret.cmd_crc);
	zassert_false(bt_in_progress);

	bt_start_rc = -EIO;
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_BT_CTLR_IMG, 3000, 0, 0, 0, 0, false,
				    false, NULL);
	zassert_equal(-EIO, ret.cmd_rc);
	zassert_equal(0, ret.cmd_len);
	zassert_false(bt_in_progress);
	bt_start_rc = 0;

	bt_fail_after = 10;
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_BT_CTLR_IMG, 4000, 0, 0, 0, 0, false,
				    false, NULL);
	zassert_equal(-EIO, ret.cmd_rc);
	zassert_false(bt_in_progress);

	bt_finish_rc = -EINVAL;
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_BT_CTLR_IMG, 3000, 0, 0, 0, 0, false,
				    false, NULL);
	zassert_equal(-EINVAL, ret.cmd_rc);
	zassert_equal(3000, ret.cmd_len);
	zassert_equal(ret.written_crc, ret.cmd_crc);
	zassert_false(bt_in_progress);
	bt_finish_rc = 0;

	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_BT_CTLR_IMG, 3000, 0, 0, 0, 0, false,
				    false, NULL);
	zassert_equal(0, ret.cmd_rc);
	zassert_equal(3000, ret.cmd_len);
	zassert_equal(ret.written_crc, ret.cmd_crc);
	zassert_false(bt_in_progress);
#endif /* CONFIG_TEST_NATIVE_MOCK */
}

ZTEST(rpc_command_file_write_basic, test_lost_payload)
{
	struct test_out ret;
	/* "Lost" data payload after some packets */
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 5, 0, 0, 0, false, false,
				    NULL);
	zassert_equal(0, ret.cmd_rc);
	zassert_true(ret.cmd_len < 1000);
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 10, 0, 0, 0, false, false,
				    NULL);
	zassert_equal(0, ret.cmd_rc);
	zassert_true(ret.cmd_len < 1000);
}

ZTEST(rpc_command_file_write_basic, test_early_hangup)
{
	struct test_out ret;

	/* Stop sending data after some packets */
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 0, 3, 0, 0, false, false,
				    NULL);
	zassert_equal(-ETIMEDOUT, ret.cmd_rc);
	zassert_true(ret.cmd_len < 1000);
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 0, 11, 0, 0, false, false,
				    NULL);
	zassert_equal(-ETIMEDOUT, ret.cmd_rc);
	zassert_true(ret.cmd_len < 1000);
}

ZTEST(rpc_command_file_write_basic, test_invalid_request_id)
{
	struct test_out ret;

	/* Inject request ID after some packets */
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 0, 0, 4, 0, false, false,
				    NULL);
	zassert_equal(0, ret.cmd_rc);
	zassert_equal(1000, ret.cmd_len);
	zassert_equal(ret.written_crc, ret.cmd_crc);
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 0, 0, 10, 0, false, false,
				    NULL);
	zassert_equal(0, ret.cmd_rc);
	zassert_equal(1000, ret.cmd_len);
	zassert_equal(ret.written_crc, ret.cmd_crc);
}

ZTEST(rpc_command_file_write_basic, test_data_ack)
{
	struct test_out ret;

	/* Generating INFUSE_DATA_ACK packets */
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 0, 0, 0, 1, false, false,
				    NULL);
	zassert_equal(0, ret.cmd_rc);
	zassert_equal(1000, ret.cmd_len);
	zassert_equal(ret.written_crc, ret.cmd_crc);
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 0, 0, 0, 2, false, false,
				    NULL);
	zassert_equal(0, ret.cmd_rc);
	zassert_equal(1000, ret.cmd_len);
	zassert_equal(ret.written_crc, ret.cmd_crc);
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 0, 0, 0, 3, false, false,
				    NULL);
	zassert_equal(0, ret.cmd_rc);
	zassert_equal(1000, ret.cmd_len);
	zassert_equal(ret.written_crc, ret.cmd_crc);
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 0, 0, 0, 4, false, false,
				    NULL);
	zassert_equal(0, ret.cmd_rc);
	zassert_equal(1000, ret.cmd_len);
	zassert_equal(ret.written_crc, ret.cmd_crc);
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 0, 0, 0,
				    RPC_SERVER_MAX_ACK_PERIOD, false, false, NULL);
	zassert_equal(0, ret.cmd_rc);
	zassert_equal(1000, ret.cmd_len);
	zassert_equal(ret.written_crc, ret.cmd_crc);
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 0, 0, 0,
				    RPC_SERVER_MAX_ACK_PERIOD + 1, false, false, NULL);
	zassert_equal(0, ret.cmd_rc);
	zassert_equal(1000, ret.cmd_len);
	zassert_equal(ret.written_crc, ret.cmd_crc);
}

ZTEST(rpc_command_file_write_basic, test_everything_wrong)
{
	struct test_out ret;

	/* Everything going wrong */
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 3, 0, 7, 1, false, false,
				    NULL);
	zassert_equal(0, ret.cmd_rc);
	zassert_true(ret.cmd_len < 1000);
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 3, 0, 7, 2, false, false,
				    NULL);
	zassert_equal(0, ret.cmd_rc);
	zassert_true(ret.cmd_len < 1000);
}

ZTEST(rpc_command_file_write_basic, test_push_too_much_data)
{
	struct test_out ret;

	/* Send too much data */
	ret = test_file_write_basic(RPC_ENUM_FILE_ACTION_DISCARD, 1000, 0, 0, 0, 0, true, false,
				    NULL);
	zassert_equal(-EINVAL, ret.cmd_rc);
}

void *file_write_basic_setup(void)
{
	sys_rand_get(fixed_payload, sizeof(fixed_payload));
	fixed_payload_crc = crc32_ieee(fixed_payload, sizeof(fixed_payload));
	return NULL;
}

ZTEST_SUITE(rpc_command_file_write_basic, NULL, file_write_basic_setup, NULL, NULL, NULL);
