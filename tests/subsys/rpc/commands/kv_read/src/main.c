/**
 * @file
 * @copyright 2024 Embeint Inc
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

static void send_kv_read_command(uint32_t request_id, uint16_t *keys, uint8_t req_num,
				 uint8_t actual_num)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_kv_read_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_KV_READ,
			},
		.num = req_num,
	};

	/* Push command at RPC server */
	epacket_dummy_receive_extra(epacket_dummy, &header, &params, sizeof(params), keys,
				    actual_num * sizeof(uint16_t));
}

static struct net_buf *expect_kv_read_response(uint32_t request_id, int rc)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_kv_read_response *response;
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

ZTEST(rpc_command_kv_read, test_kv_read_bad_input)
{
	struct net_buf *rsp;
	uint16_t keys[2];

	/* Ask to read 2 keys, only give info for 1 */
	keys[0] = KV_KEY_REBOOTS;
	keys[1] = KV_KEY_WIFI_SSID;
	send_kv_read_command(1000, keys, 2, 1);
	rsp = expect_kv_read_response(1000, -EINVAL);
	net_buf_unref(rsp);
}

ZTEST(rpc_command_kv_read, test_single)
{
	KV_STRING_CONST(test_psk, "AAAAAAAA");
	KV_KEY_TYPE_VAR(KV_KEY_WIFI_PSK, 64) test_read;
	struct rpc_kv_read_response *response;
	struct net_buf *rsp;
	uint16_t key;

	/* Read a single key that exists */
	key = KV_KEY_REBOOTS;
	send_kv_read_command(0x1234, &key, 1, 1);
	rsp = expect_kv_read_response(0x1234, 0);
	response = (void *)rsp->data;
	zassert_equal(sizeof(struct rpc_kv_read_response) +
			      sizeof(struct rpc_struct_kv_store_value) + sizeof(uint32_t),
		      rsp->len);
	zassert_equal(KV_KEY_REBOOTS, response->values[0].id);
	zassert_equal(sizeof(uint32_t), response->values[0].len);
	zassert_equal(1, sys_get_le32(response->values[0].data));
	net_buf_unref(rsp);

	/* Read a single key that is enabled but has not been written */
	key = KV_KEY_WIFI_SSID;
	send_kv_read_command(1000, &key, 1, 1);
	rsp = expect_kv_read_response(1000, 0);
	response = (void *)rsp->data;
	zassert_equal(sizeof(struct rpc_kv_read_response) +
			      sizeof(struct rpc_struct_kv_store_value),
		      rsp->len);
	zassert_equal(KV_KEY_WIFI_SSID, response->values[0].id);
	zassert_equal(-ENOENT, response->values[0].len);
	net_buf_unref(rsp);

	/* Read a single key that is disabled */
	key = 0x4567;
	send_kv_read_command(1001, &key, 1, 1);
	rsp = expect_kv_read_response(1001, 0);
	response = (void *)rsp->data;
	zassert_equal(sizeof(struct rpc_kv_read_response) +
			      sizeof(struct rpc_struct_kv_store_value),
		      rsp->len);
	zassert_equal(0x4567, response->values[0].id);
	zassert_equal(-EACCES, response->values[0].len);
	net_buf_unref(rsp);

	/* Read a single key that is enabled, hasn't been written, has readback protection */
	key = KV_KEY_WIFI_PSK;
	zassert_equal(-ENOENT, kv_store_read(key, &test_read, sizeof(test_read)));
	send_kv_read_command(1002, &key, 1, 1);
	rsp = expect_kv_read_response(1002, 0);
	response = (void *)rsp->data;
	zassert_equal(sizeof(struct rpc_kv_read_response) +
			      sizeof(struct rpc_struct_kv_store_value),
		      rsp->len);
	zassert_equal(KV_KEY_WIFI_PSK, response->values[0].id);
	zassert_equal(-EPERM, response->values[0].len);
	net_buf_unref(rsp);

	/* Read a single key that is enabled, has been written, has readback protection */
	key = KV_KEY_WIFI_PSK;
	kv_store_write(key, &test_psk, sizeof(test_psk));
	send_kv_read_command(1003, &key, 1, 1);
	rsp = expect_kv_read_response(1003, 0);
	response = (void *)rsp->data;
	zassert_equal(sizeof(struct rpc_kv_read_response) +
			      sizeof(struct rpc_struct_kv_store_value),
		      rsp->len);
	zassert_equal(KV_KEY_WIFI_PSK, response->values[0].id);
	zassert_equal(-EPERM, response->values[0].len);
	net_buf_unref(rsp);
}

ZTEST(rpc_command_kv_read, test_multi_valid)
{
	KV_STRING_CONST(test_string, "TEST STRING");
	struct rpc_struct_kv_store_value *value;
	struct net_buf *rsp;
	uint16_t keys[2];

	/* Write a second value */
	(void)kv_store_write(KV_KEY_WIFI_SSID, &test_string, sizeof(test_string));

	/* Read two keys that exist */
	keys[0] = KV_KEY_REBOOTS;
	keys[1] = KV_KEY_WIFI_SSID;
	send_kv_read_command(500, keys, 2, 2);
	rsp = expect_kv_read_response(500, 0);
	net_buf_pull(rsp, sizeof(struct rpc_kv_read_response));

	/* Test first value */
	value = (void *)rsp->data;
	zassert_equal(KV_KEY_REBOOTS, value->id);
	zassert_equal(sizeof(uint32_t), value->len);
	zassert_equal(1, sys_get_le32(value->data));
	net_buf_pull(rsp, sizeof(struct rpc_struct_kv_store_value) + sizeof(uint32_t));

	/* Test second value */
	value = (void *)rsp->data;
	zassert_equal(KV_KEY_WIFI_SSID, value->id);
	zassert_equal(sizeof(test_string), value->len);
	zassert_mem_equal(&test_string, value->data, value->len);
	net_buf_pull(rsp, sizeof(struct rpc_struct_kv_store_value) + sizeof(test_string));

	/* Should be no data left on buffer */
	zassert_equal(0, rsp->len);
	net_buf_unref(rsp);

	/* Cleanup key added */
	zassert_equal(0, kv_store_delete(KV_KEY_WIFI_SSID));
}

ZTEST(rpc_command_kv_read, test_multi_invalid)
{
	KV_STRING_CONST(test_string, "TEST STRING");
	struct rpc_struct_kv_store_value *value;
	struct net_buf *rsp;
	uint16_t keys[2];

	/* Write a second value */
	(void)kv_store_write(KV_KEY_WIFI_SSID, &test_string, sizeof(test_string));

	/* Read error followed by valid data */
	keys[0] = 0x1234;
	keys[1] = KV_KEY_WIFI_SSID;
	send_kv_read_command(500, keys, 2, 2);
	rsp = expect_kv_read_response(500, 0);
	net_buf_pull(rsp, sizeof(struct rpc_kv_read_response));

	/* Test first value failed */
	value = (void *)rsp->data;
	zassert_equal(0x1234, value->id);
	zassert_equal(-EACCES, value->len);
	net_buf_pull(rsp, sizeof(struct rpc_struct_kv_store_value));

	/* Test second value worked */
	value = (void *)rsp->data;
	zassert_equal(KV_KEY_WIFI_SSID, value->id);
	zassert_equal(sizeof(test_string), value->len);
	zassert_mem_equal(&test_string, value->data, value->len);
	net_buf_pull(rsp, sizeof(struct rpc_struct_kv_store_value) + sizeof(test_string));

	/* Should be no data left on buffer */
	zassert_equal(0, rsp->len);
	net_buf_unref(rsp);

	/* Cleanup key added */
	zassert_equal(0, kv_store_delete(KV_KEY_WIFI_SSID));
}

ZTEST(rpc_command_kv_read, test_too_large)
{
	KV_STRING_CONST(test_string, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
	struct rpc_struct_kv_store_value *value;
	struct net_buf *rsp;
	uint16_t keys[2];

	/* Write a too large value */
	(void)kv_store_write(KV_KEY_WIFI_SSID, &test_string, sizeof(test_string));

	/* Read error followed by valid data */
	keys[0] = KV_KEY_REBOOTS;
	keys[1] = KV_KEY_WIFI_SSID;
	send_kv_read_command(100, keys, 2, 2);
	rsp = expect_kv_read_response(100, 0);
	net_buf_pull(rsp, sizeof(struct rpc_kv_read_response));

	/* Test first value worked */
	value = (void *)rsp->data;
	zassert_equal(KV_KEY_REBOOTS, value->id);
	zassert_equal(sizeof(uint32_t), value->len);
	zassert_equal(1, sys_get_le32(value->data));
	net_buf_pull(rsp, sizeof(struct rpc_struct_kv_store_value) + sizeof(uint32_t));

	/* Test second value failed */
	value = (void *)rsp->data;
	zassert_equal(KV_KEY_WIFI_SSID, value->id);
	zassert_equal(-ENOSPC, value->len);
	net_buf_pull(rsp, sizeof(struct rpc_struct_kv_store_value));

	/* Should be no data left on buffer */
	zassert_equal(0, rsp->len);
	net_buf_unref(rsp);

	/* Try again with enough space */
	keys[0] = KV_KEY_WIFI_SSID;
	send_kv_read_command(101, keys, 1, 1);
	rsp = expect_kv_read_response(101, 0);
	net_buf_pull(rsp, sizeof(struct rpc_kv_read_response));

	/* Test second value worked */
	value = (void *)rsp->data;
	zassert_equal(KV_KEY_WIFI_SSID, value->id);
	zassert_equal(sizeof(test_string), value->len);
	zassert_mem_equal(&test_string, value->data, value->len);
	net_buf_pull(rsp, sizeof(struct rpc_struct_kv_store_value) + sizeof(test_string));

	/* Should be no data left on buffer */
	zassert_equal(0, rsp->len);
	net_buf_unref(rsp);

	/* Cleanup key added */
	zassert_equal(0, kv_store_delete(KV_KEY_WIFI_SSID));
}

ZTEST(rpc_command_kv_read, test_no_payload)
{
	struct rpc_kv_read_response *response;
	struct net_buf *rsp;
	uint16_t key;

	/* Read a single key that exists, but no space for response */
	key = KV_KEY_REBOOTS;
	send_kv_read_command(0x1238, &key, 1, 1);
	epacket_dummy_set_max_packet(sizeof(struct epacket_dummy_frame) +
				     sizeof(struct infuse_rpc_rsp_header) + 1);

	rsp = expect_kv_read_response(0x1238, 0);
	response = (void *)rsp->data;
	/* No key values, just the header */
	zassert_equal(sizeof(struct rpc_kv_read_response), rsp->len);
	net_buf_unref(rsp);
}

static void test_before(void *data)
{
	epacket_dummy_set_max_packet(CONFIG_EPACKET_PACKET_SIZE_MAX);
}

ZTEST_SUITE(rpc_command_kv_read, NULL, NULL, test_before, NULL, NULL);
