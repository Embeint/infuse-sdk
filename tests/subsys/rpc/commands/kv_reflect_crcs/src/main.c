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

#include <infuse/common_boot.h>
#include <infuse/types.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/rpc/types.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>

/* From kv_internal.h */
uint32_t kv_reflect_key_crc(size_t reflect_idx);

static void send_kv_reflect_crcs_command(uint32_t request_id, uint8_t offset)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_kv_reflect_crcs_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_KV_REFLECT_CRCS,
			},
		.offset = offset,
	};

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static struct net_buf *expect_kv_reflect_crcs_response(uint32_t request_id, int rc)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_kv_reflect_crcs_response *response;
	struct net_buf *rsp;

	zassert_not_null(response_queue);

	/* Response was sent */
	rsp = k_fifo_get(response_queue, K_MSEC(100));
	zassert_not_null(rsp);
	net_buf_pull_mem(rsp, sizeof(struct epacket_dummy_frame));
	response = (void *)rsp->data;

	/* Parameters match what we expect */
	zassert_equal(request_id, response->header.request_id);
	zassert_equal(rc, response->header.return_code);

	/* Return the response */
	return rsp;
}

ZTEST(rpc_command_kv_reflect_crcs, test_kv_reflect_crcs_basic)
{
	struct rpc_kv_reflect_crcs_response *response;
	struct net_buf *rsp;
	size_t expect_len;

	send_kv_reflect_crcs_command(1000, 0);
	rsp = expect_kv_reflect_crcs_response(1000, 0);

	response = (void *)rsp->data;
	zassert_equal(KV_REFLECT_NUM, response->num);
	zassert_equal(0, response->remaining);
	expect_len = sizeof(struct rpc_kv_reflect_crcs_response) +
		     response->num * sizeof(struct rpc_struct_kv_store_crc);
	zassert_equal(expect_len, rsp->len);

	for (int i = 0; i < response->num; i++) {
		zassert_equal(kv_reflect_key_crc(i), response->crcs[i].crc);
	}

	net_buf_unref(rsp);
}

ZTEST(rpc_command_kv_reflect_crcs, test_kv_reflect_crcs_overflow)
{
	struct rpc_kv_reflect_crcs_response *response;
	struct net_buf *rsp;
	size_t expect_len;

	/* Limit payload size:
	 *   8 byte dummy header
	 *   0 byte dummy footer
	 *   8 byte RPC response header
	 *   4 byte Command response header
	 *
	 * 6 bytes per ID:CRC pair, should fit 2.
	 */
	epacket_dummy_set_max_packet(36);

	send_kv_reflect_crcs_command(1001, 0);
	rsp = expect_kv_reflect_crcs_response(1001, 0);

	response = (void *)rsp->data;
	zassert_equal(2, response->num);
	zassert_equal(KV_REFLECT_NUM - 2, response->remaining);
	expect_len = sizeof(struct rpc_kv_reflect_crcs_response) +
		     response->num * sizeof(struct rpc_struct_kv_store_crc);
	zassert_equal(expect_len, rsp->len);

	for (int i = 0; i < response->num; i++) {
		zassert_equal(kv_reflect_key_crc(i), response->crcs[i].crc);
	}

	net_buf_unref(rsp);
}

ZTEST(rpc_command_kv_reflect_crcs, test_kv_reflect_crcs_offset)
{
	struct rpc_kv_reflect_crcs_response *response;
	struct net_buf *rsp;
	size_t expect_len;

	send_kv_reflect_crcs_command(1002, 1);
	rsp = expect_kv_reflect_crcs_response(1002, 0);

	response = (void *)rsp->data;
	zassert_equal(KV_REFLECT_NUM - 1, response->num);
	zassert_equal(0, response->remaining);
	expect_len = sizeof(struct rpc_kv_reflect_crcs_response) +
		     response->num * sizeof(struct rpc_struct_kv_store_crc);
	zassert_equal(expect_len, rsp->len);

	for (int i = 0; i < response->num; i++) {
		zassert_equal(kv_reflect_key_crc(i + 1), response->crcs[i].crc);
	}

	net_buf_unref(rsp);
}

static void *kv_setup(void)
{
	KV_KEY_TYPE_VAR(KV_KEY_GEOFENCE, 2) geofence1 = {2, {{1, 2, 3}, {4, 5, 6}}};
	KV_KEY_TYPE_VAR(KV_KEY_GEOFENCE, 2) geofence2 = {2, {{7, 8, 9}, {1, 2, 3}}};
	KV_KEY_TYPE_VAR(KV_KEY_GEOFENCE, 2) geofence3 = {2, {{4, 5, 6}, {9, 8, 7}}};
	int kv_store_init(void);

	zassert_equal(0, kv_store_init());
	zassert_equal(0, kv_store_reset());

	zassert_equal(sizeof(geofence1), KV_STORE_WRITE(KV_KEY_GEOFENCE + 0, &geofence1));
	zassert_equal(sizeof(geofence2), KV_STORE_WRITE(KV_KEY_GEOFENCE + 1, &geofence2));
	zassert_equal(sizeof(geofence3), KV_STORE_WRITE(KV_KEY_GEOFENCE + 2, &geofence3));

	epacket_dummy_set_max_packet(UINT16_MAX);
	return NULL;
}

ZTEST_SUITE(rpc_command_kv_reflect_crcs, NULL, kv_setup, NULL, NULL, NULL);
