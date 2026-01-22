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
#include <zephyr/drivers/hwinfo.h>

#include <infuse/common_boot.h>
#include <infuse/types.h>
#include <infuse/security.h>
#include <infuse/identifiers.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/rpc/types.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>

#include <psa/crypto.h>

static void send_security_public_keys_command(uint32_t request_id, uint8_t skip)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_NETWORK,
		.flags = 0x0000,
	};
	struct rpc_security_public_keys_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_SECURITY_PUBLIC_KEYS,
			},
		.skip = skip,
	};

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static struct net_buf *expect_security_public_keys_response(uint32_t request_id, uint8_t num_total,
							    uint8_t num_returned)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_security_public_keys_response *response;
	size_t expected_len;
	struct net_buf *rsp;

	zassert_not_null(response_queue);

	/* Response was sent */
	rsp = k_fifo_get(response_queue, K_SECONDS(10));
	zassert_not_null(rsp);
	net_buf_pull_mem(rsp, sizeof(struct epacket_dummy_frame));
	response = (void *)rsp->data;

	/* Parameters match what we expect */
	zassert_equal(request_id, response->header.request_id);
	zassert_equal(0, response->header.return_code);
	zassert_equal(num_total, response->keys_total);
	zassert_equal(num_returned, response->keys_included);

	/* Validate returned size */
	expected_len = sizeof(*response) +
		       (num_returned * sizeof(struct rpc_struct_public_key_info_256bit));
	zassert_equal(expected_len, rsp->len);

	/* Return the response */
	return rsp;
}

ZTEST(rpc_command_security_public_keys, test_security_public_keys)
{
	struct kv_secondary_remote_public_key remote;
	struct rpc_security_public_keys_response *response;
	struct net_buf *rsp;

	/* No secondary key */
	send_security_public_keys_command(0x100, 0);
	rsp = expect_security_public_keys_response(0x100, 2, 2);
	response = (void *)rsp->data;
	zassert_equal(RPC_ENUM_KEY_ID_DEVICE_PUBLIC_KEY, response->public_keys[0].id);
	zassert_equal(RPC_ENUM_KEY_ID_CLOUD_PUBLIC_KEY, response->public_keys[1].id);
	net_buf_unref(rsp);

	/* Skip first */
	send_security_public_keys_command(0x101, 1);
	rsp = expect_security_public_keys_response(0x101, 2, 1);
	response = (void *)rsp->data;
	zassert_equal(RPC_ENUM_KEY_ID_CLOUD_PUBLIC_KEY, response->public_keys[0].id);
	net_buf_unref(rsp);

	/* Add the secondary key */
	sys_rand_get(remote.public_key, sizeof(remote.public_key));
	zassert_equal(sizeof(remote), KV_STORE_WRITE(KV_KEY_SECONDARY_REMOTE_PUBLIC_KEY, &remote));

	/* All 3 returned */
	send_security_public_keys_command(0x102, 0);
	rsp = expect_security_public_keys_response(0x102, 3, 3);
	response = (void *)rsp->data;
	zassert_equal(RPC_ENUM_KEY_ID_DEVICE_PUBLIC_KEY, response->public_keys[0].id);
	zassert_equal(RPC_ENUM_KEY_ID_CLOUD_PUBLIC_KEY, response->public_keys[1].id);
	zassert_equal(RPC_ENUM_KEY_ID_SECONDARY_REMOTE_PUBLIC_KEY, response->public_keys[2].id);
	net_buf_unref(rsp);

	/* Limit backend size */
	epacket_dummy_set_max_packet(100);

	/* First 2 returned */
	send_security_public_keys_command(0x103, 0);
	rsp = expect_security_public_keys_response(0x103, 3, 2);
	response = (void *)rsp->data;
	zassert_equal(RPC_ENUM_KEY_ID_DEVICE_PUBLIC_KEY, response->public_keys[0].id);
	zassert_equal(RPC_ENUM_KEY_ID_CLOUD_PUBLIC_KEY, response->public_keys[1].id);
	net_buf_unref(rsp);

	/* Query the missing one */
	send_security_public_keys_command(0x103, 2);
	rsp = expect_security_public_keys_response(0x103, 3, 1);
	response = (void *)rsp->data;
	zassert_equal(RPC_ENUM_KEY_ID_SECONDARY_REMOTE_PUBLIC_KEY, response->public_keys[0].id);
	net_buf_unref(rsp);
}

ZTEST_SUITE(rpc_command_security_public_keys, NULL, NULL, NULL, NULL, NULL);
