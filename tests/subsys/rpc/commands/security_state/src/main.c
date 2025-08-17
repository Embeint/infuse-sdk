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
#include <infuse/rpc/commands/security_state.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>

#include <psa/crypto.h>

static void send_security_state_command(uint32_t request_id, uint8_t challenge[16])
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_NETWORK,
		.flags = 0x0000,
	};
	struct rpc_security_state_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_SECURITY_STATE,
			},
	};
	memcpy(params.challenge, challenge, sizeof(params.challenge));

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static struct net_buf *expect_security_state_response(uint32_t request_id)
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
	zassert_equal(0, response->header.return_code);

	/* Return the response */
	return rsp;
}

ZTEST(rpc_command_security_state, test_security_state)
{
	psa_key_id_t sign_key = infuse_security_device_sign_key();
	struct rpc_security_state_response *response;
	struct security_state_response_hw_id_encrypted *hwid_encrypted;
	struct security_state_response_hw_id hwid;
	size_t response_len, expected;
	uint8_t hw_id[sizeof(hwid.hardware_id)] = {0x00};
	uint8_t challenge[16];
	struct net_buf *rsp;
	psa_status_t status;
	size_t olen;

	zassert_not_equal(PSA_KEY_ID_NULL, sign_key);
	hwinfo_get_device_id(hw_id, sizeof(hw_id));

	/* Get random challenge bytes */
	sys_rand_get(challenge, sizeof(challenge));

	send_security_state_command(0x100, challenge);
	rsp = expect_security_state_response(0x100);
	response = (void *)rsp->data;
	hwid_encrypted = (void *)response->challenge_response;

	response_len = rsp->len - sizeof(*response);

	/* Validate challenge response */
	zassert_equal(CHALLENGE_RESPONSE_HARDWARE_ID, response->challenge_response_type);
	/* Nonce + Challenge + Secret + ID + Tag */
	expected = 12 + 16 + 16 + 8 + 16;
	zassert_equal(expected, response_len);

	/* Encrypted challenge can be decrypted */
	status = psa_aead_decrypt(sign_key, PSA_ALG_CHACHA20_POLY1305, hwid_encrypted->nonce,
				  sizeof(hwid_encrypted->nonce), response->cloud_public_key, 69,
				  (void *)&hwid_encrypted->ciphertext,
				  sizeof(hwid_encrypted->ciphertext), (void *)&hwid, sizeof(hwid),
				  &olen);
	zassert_equal(PSA_SUCCESS, status);
	zassert_equal(sizeof(hwid), olen);

	/* Challenge contents */
	zassert_equal(infuse_device_id(), hwid.device_id);
	zassert_mem_equal(challenge, hwid.challenge, sizeof(challenge));
	zassert_mem_equal(hw_id, hwid.hardware_id, sizeof(hwid.hardware_id));

	net_buf_unref(rsp);
}

ZTEST_SUITE(rpc_command_security_state, NULL, NULL, NULL, NULL, NULL);
