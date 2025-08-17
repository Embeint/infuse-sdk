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

#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/rpc/types.h>
#include <infuse/rpc/commands.h>
#include <infuse/tdf/tdf.h>
#include <infuse/tdf/definitions.h>

static int rpc_ext1_calls;

/* Implementation of our dummy extension command */
struct net_buf *rpc_command_ext1(struct net_buf *request)
{
	struct rpc_ext1_request *req = (void *)request->data;
	struct rpc_ext1_response rsp = {
		.rsp1 = req->arg1.x + req->arg1.y + req->arg1.z,
	};

	rpc_ext1_calls += 1;

	/* Allocate the response packet */
	return rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));
}

static void send_ext1_command(uint32_t request_id)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_ext1_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_EXT1,
			},
		.arg1 = {1, 2, 3},
	};

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static struct net_buf *expect_ext1_response(uint32_t request_id)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_ext1_response *response;
	struct net_buf *rsp;

	zassert_not_null(response_queue);

	/* Response was sent */
	rsp = k_fifo_get(response_queue, K_MSEC(100));
	zassert_not_null(rsp);
	net_buf_pull_mem(rsp, sizeof(struct epacket_dummy_frame));
	response = (void *)rsp->data;

	/* Parameters match what we expect */
	zassert_equal(request_id, response->header.request_id);
	zassert_equal(0, response->header.return_code);

	/* Return the response */
	return rsp;
}

ZTEST(definitions_extend, test_ext_rpc)
{
	struct net_buf *rsp;

	zassert_equal(0, rpc_ext1_calls);

	send_ext1_command(0x1234);
	rsp = expect_ext1_response(0x1234);
	net_buf_unref(rsp);

	zassert_equal(1, rpc_ext1_calls);
}

ZTEST(definitions_extend, test_ext_tdf)
{
	TDF_TYPE(TDF_EXT1) ext1 = {0};
	TDF_TYPE(TDF_EXT2) ext2 = {0};
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct tdf_parsed tdf;
	struct net_buf *tx;

	tdf_data_logger_log(TDF_DATA_LOGGER_SERIAL, TDF_EXT1, sizeof(ext1), 0, &ext1);
	tdf_data_logger_log(TDF_DATA_LOGGER_SERIAL, TDF_EXT2, sizeof(ext2), 0, &ext2);
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	tx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_not_null(tx);
	net_buf_pull(tx, sizeof(struct epacket_dummy_frame));
	zassert_equal(0, tdf_parse_find_in_buf(tx->data, tx->len, TDF_EXT1, &tdf));
	zassert_equal(0, tdf_parse_find_in_buf(tx->data, tx->len, TDF_EXT2, &tdf));
	net_buf_unref(tx);
}

ZTEST(definitions_extend, test_ext_kv_store)
{
	KV_KEY_TYPE(KV_KEY_EXT1) kv_ext1 = {.a = 0x1234}, rb_ext1;
	KV_KEY_TYPE(KV_KEY_EXT2) kv_ext2 = {.a.y = -5}, rb_ext2;
	KV_KEY_TYPE(KV_KEY_EXT3) kv_ext3 = {0};

	zassert_true(kv_store_key_enabled(KV_KEY_EXT1));
	zassert_true(kv_store_key_enabled(KV_KEY_EXT2 + 0));
	zassert_true(kv_store_key_enabled(KV_KEY_EXT2 + 1));
	zassert_false(kv_store_key_enabled(KV_KEY_EXT3));

	zassert_false(kv_store_key_exists(KV_KEY_EXT1));
	zassert_false(kv_store_key_exists(KV_KEY_EXT2 + 0));
	zassert_false(kv_store_key_exists(KV_KEY_EXT2 + 1));
	zassert_false(kv_store_key_exists(KV_KEY_EXT3));

	zassert_equal(sizeof(kv_ext1), KV_STORE_WRITE(KV_KEY_EXT1, &kv_ext1));
	zassert_equal(sizeof(kv_ext2), KV_STORE_WRITE(KV_KEY_EXT2 + 0, &kv_ext2));
	zassert_equal(sizeof(kv_ext2), KV_STORE_WRITE(KV_KEY_EXT2 + 1, &kv_ext2));
	zassert_equal(-EACCES, KV_STORE_WRITE(KV_KEY_EXT3, &kv_ext3));

	zassert_equal(sizeof(kv_ext1), KV_STORE_READ(KV_KEY_EXT1, &rb_ext1));
	zassert_equal(sizeof(kv_ext2), KV_STORE_READ(KV_KEY_EXT2 + 0, &rb_ext2));
	zassert_equal(sizeof(kv_ext2), KV_STORE_READ(KV_KEY_EXT2 + 1, &rb_ext2));
	zassert_equal(-EACCES, KV_STORE_READ(KV_KEY_EXT3, &kv_ext3));

	zassert_mem_equal(&kv_ext1, &rb_ext1, sizeof(kv_ext1));
	zassert_mem_equal(&kv_ext2, &rb_ext2, sizeof(kv_ext2));

	zassert_true(kv_store_key_exists(KV_KEY_EXT1));
	zassert_true(kv_store_key_exists(KV_KEY_EXT2 + 0));
	zassert_true(kv_store_key_exists(KV_KEY_EXT2 + 1));
	zassert_false(kv_store_key_exists(KV_KEY_EXT3));
}

ZTEST_SUITE(definitions_extend, NULL, NULL, NULL, NULL, NULL);
