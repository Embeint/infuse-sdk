/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>
#include <zephyr/random/random.h>

#include <infuse/common_boot.h>
#include <infuse/types.h>
#include <infuse/reboot.h>
#include <infuse/security.h>
#include <infuse/rpc/types.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

#include <psa/crypto.h>

K_SEM_DEFINE(reboot_request, 0, 1);

int infuse_reboot_state_query(struct infuse_reboot_state *state)
{
	return -ENOENT;
}

void infuse_reboot(enum infuse_reboot_reason reason, uint32_t info1, uint32_t info2)
{
	k_sem_give(&reboot_request);
}

void infuse_reboot_delayed(enum infuse_reboot_reason reason, uint32_t info1, uint32_t info2,
			   k_timeout_t delay)
{
	k_sem_give(&reboot_request);
}

static void send_security_key_update_command(uint32_t request_id, uint8_t key_id,
					     uint8_t key_action, uint32_t key_global_id,
					     uint8_t key_val[32], uint8_t reboot_delay)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_security_key_update_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_SECURITY_KEY_UPDATE,
			},
		.key_id = key_id,
		.key_action = key_action,
		.key_global_identifier = key_global_id,
		.reboot_delay = reboot_delay,
	};
	memcpy(params.key_bitstream, key_val, sizeof(params.key_bitstream));

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static void expect_security_key_update_response(uint32_t request_id, int expected_rc)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_security_state_response *response;
	struct net_buf *rsp;

	zassert_not_null(response_queue);

	/* Response was sent */
	rsp = k_fifo_get(response_queue, K_SECONDS(10));
	zassert_not_null(rsp);
	net_buf_pull_mem(rsp, sizeof(struct epacket_dummy_frame));
	response = (void *)rsp->data;

	/* Parameters match what we expect */
	zassert_equal(request_id, response->header.request_id);
	zassert_equal(expected_rc, response->header.return_code);

	/* Return the response */
	net_buf_unref(rsp);
}

ZTEST(rpc_command_security_key_update, test_invalid)
{
	uint8_t bitstream[32];

	sys_rand_get(bitstream, sizeof(bitstream));

	/* Bad key ID */
	send_security_key_update_command(0x106, 3, RPC_ENUM_KEY_ACTION_KEY_WRITE, 0x123456,
					 bitstream, 5);
	expect_security_key_update_response(0x106, -EINVAL);
	zassert_equal(-EAGAIN, k_sem_take(&reboot_request, K_MSEC(100)));

	/* Bad action */
	send_security_key_update_command(0x210, RPC_ENUM_KEY_ID_NETWORK_KEY, 2, 0x123456, bitstream,
					 5);
	expect_security_key_update_response(0x210, -EINVAL);
	zassert_equal(-EAGAIN, k_sem_take(&reboot_request, K_MSEC(100)));
}

ZTEST(rpc_command_security_key_update, test_primary_network_keys)
{
	uint8_t bitstream[32];

	sys_rand_get(bitstream, sizeof(bitstream));

	/* Initial network state */
	zassert_equal(0x000000, infuse_security_network_key_identifier());
	zassert_equal(0xFFFFFF, infuse_security_secondary_network_key_identifier());

	/* Write new default network key, no reboot */
	send_security_key_update_command(0x100, RPC_ENUM_KEY_ID_NETWORK_KEY,
					 RPC_ENUM_KEY_ACTION_KEY_WRITE, 0x123456, bitstream, 0);
	expect_security_key_update_response(0x100, 0);
	zassert_equal(-EAGAIN, k_sem_take(&reboot_request, K_MSEC(100)));

	/* Once the init function runs again, new keys are used */
	infuse_security_network_keys_unload();
	infuse_security_network_keys_load();

	zassert_equal(0x123456, infuse_security_network_key_identifier());
	zassert_equal(0xFFFFFF, infuse_security_secondary_network_key_identifier());

	/* Delete updated key, reboot */
	send_security_key_update_command(0x101, RPC_ENUM_KEY_ID_NETWORK_KEY,
					 RPC_ENUM_KEY_ACTION_KEY_DELETE, 0x123456, bitstream, 4);
	expect_security_key_update_response(0x101, 0);
	zassert_equal(0, k_sem_take(&reboot_request, K_MSEC(100)));

	/* Once the init function runs again, default keys are used */
	infuse_security_network_keys_unload();
	infuse_security_network_keys_load();

	zassert_equal(0x000000, infuse_security_network_key_identifier());
	zassert_equal(0xFFFFFF, infuse_security_secondary_network_key_identifier());

	/* Delete again, no reboot */
	send_security_key_update_command(0x102, RPC_ENUM_KEY_ID_NETWORK_KEY,
					 RPC_ENUM_KEY_ACTION_KEY_DELETE, 0x123456, bitstream, 4);
	expect_security_key_update_response(0x102, PSA_ERROR_DOES_NOT_EXIST);
	zassert_equal(-EAGAIN, k_sem_take(&reboot_request, K_MSEC(100)));
}

ZTEST(rpc_command_security_key_update, test_secondary_network_keys)
{
	uint8_t bitstream[32];

	sys_rand_get(bitstream, sizeof(bitstream));

	/* Initial network state */
	zassert_equal(0x000000, infuse_security_network_key_identifier());
	zassert_equal(0xFFFFFF, infuse_security_secondary_network_key_identifier());

	/* Write new secondary network key, reboot */
	send_security_key_update_command(0x200, RPC_ENUM_KEY_ID_SECONDARY_NETWORK_KEY,
					 RPC_ENUM_KEY_ACTION_KEY_WRITE, 0x78AB32, bitstream, 2);
	expect_security_key_update_response(0x200, 0);
	zassert_equal(0, k_sem_take(&reboot_request, K_MSEC(100)));

	/* Once the init function runs again, new keys are used */
	infuse_security_network_keys_unload();
	infuse_security_network_keys_load();

	zassert_equal(0x000000, infuse_security_network_key_identifier());
	zassert_equal(0x78AB32, infuse_security_secondary_network_key_identifier());

	/* Delete updated key, no reboot */
	send_security_key_update_command(0x201, RPC_ENUM_KEY_ID_SECONDARY_NETWORK_KEY,
					 RPC_ENUM_KEY_ACTION_KEY_DELETE, 0x78AB32, bitstream, 0);
	expect_security_key_update_response(0x201, 0);
	zassert_equal(-EAGAIN, k_sem_take(&reboot_request, K_MSEC(100)));

	/* Once the init function runs again, default keys are used */
	infuse_security_network_keys_unload();
	infuse_security_network_keys_load();

	zassert_equal(0x000000, infuse_security_network_key_identifier());
	zassert_equal(0xFFFFFF, infuse_security_secondary_network_key_identifier());

	/* Delete again, no reboot */
	send_security_key_update_command(0x202, RPC_ENUM_KEY_ID_SECONDARY_NETWORK_KEY,
					 RPC_ENUM_KEY_ACTION_KEY_DELETE, 0x78AB32, bitstream, 0);
	expect_security_key_update_response(0x202, PSA_ERROR_DOES_NOT_EXIST);
	zassert_equal(-EAGAIN, k_sem_take(&reboot_request, K_MSEC(100)));
}

ZTEST(rpc_command_security_key_update, test_secondary_remote)
{
	struct kv_secondary_remote_public_key remote_public_key;
	uint8_t bitstream[32];

	sys_rand_get(bitstream, sizeof(bitstream));

	zassert_equal(-ENOENT,
		      KV_STORE_READ(KV_KEY_SECONDARY_REMOTE_PUBLIC_KEY, &remote_public_key));

	/* Write new secondary remote, reboot */
	send_security_key_update_command(0x300, RPC_ENUM_KEY_ID_SECONDARY_REMOTE_PUBLIC_KEY,
					 RPC_ENUM_KEY_ACTION_KEY_WRITE, 0, bitstream, 2);
	expect_security_key_update_response(0x300, 0);
	zassert_equal(0, k_sem_take(&reboot_request, K_MSEC(100)));

	zassert_equal(sizeof(remote_public_key),
		      KV_STORE_READ(KV_KEY_SECONDARY_REMOTE_PUBLIC_KEY, &remote_public_key));
	zassert_mem_equal(bitstream, remote_public_key.public_key, sizeof(bitstream));

	/* Delete secondary remote, reboot */
	send_security_key_update_command(0x301, RPC_ENUM_KEY_ID_SECONDARY_REMOTE_PUBLIC_KEY,
					 RPC_ENUM_KEY_ACTION_KEY_DELETE, 0, bitstream, 2);
	expect_security_key_update_response(0x301, 0);
	zassert_equal(0, k_sem_take(&reboot_request, K_MSEC(100)));

	zassert_equal(-ENOENT,
		      KV_STORE_READ(KV_KEY_SECONDARY_REMOTE_PUBLIC_KEY, &remote_public_key));
}

static void test_before(void *fixture)
{
	/* Refresh network keys to default state */
	(void)infuse_security_network_key_write(0, NULL);
	(void)infuse_security_secondary_network_key_write(0, NULL);
	infuse_security_network_keys_unload();
	infuse_security_network_keys_load();
}

ZTEST_SUITE(rpc_command_security_key_update, NULL, NULL, test_before, NULL, NULL);
