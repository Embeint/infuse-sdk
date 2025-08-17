/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/drivers/hwinfo.h>

#include <infuse/identifiers.h>
#include <infuse/security.h>
#include <infuse/rpc/commands.h>
#include <infuse/rpc/commands/security_state.h>
#include <infuse/rpc/types.h>

#include <psa/crypto.h>
#include <mbedtls/platform_util.h>

LOG_MODULE_DECLARE(rpc_server);

struct net_buf *rpc_command_security_state(struct net_buf *request)
{
	struct rpc_security_state_request *req = (void *)request->data;
	psa_key_id_t sign_key = infuse_security_device_sign_key();
	struct rpc_security_state_response rsp_header = {0};
	struct security_state_response_hw_id challenge_response;
	struct security_state_response_hw_id_encrypted *rsp;
	struct net_buf *rsp_buf;
	psa_status_t status;
	size_t ad_len, olen;

	/* Populate security state */
	infuse_security_cloud_public_key(rsp_header.cloud_public_key);
	infuse_security_device_public_key(rsp_header.device_public_key);
	rsp_header.network_id = infuse_security_network_key_identifier();
	rsp_header.challenge_response_type = CHALLENGE_RESPONSE_HARDWARE_ID;

	/* Allocate response */
	rsp_buf = rpc_response_simple_req(request, 0, &rsp_header, sizeof(rsp_header));
	rsp = net_buf_add(rsp_buf, sizeof(*rsp));

	/* Populate hardware ID  */
	memcpy(challenge_response.challenge, req->challenge, sizeof(req->challenge));
	memset(challenge_response.hardware_id, 0x00, sizeof(challenge_response.hardware_id));
	(void)hwinfo_get_device_id(challenge_response.hardware_id,
				   sizeof(challenge_response.hardware_id));
	challenge_response.device_id = infuse_device_id();

	/* Encrypt the challenge response */
	ad_len = offsetof(struct rpc_security_state_response, challenge_response) -
		 offsetof(struct rpc_security_state_response, cloud_public_key);
	sys_csrand_get(rsp->nonce, sizeof(rsp->nonce));
	status = psa_aead_encrypt(sign_key, PSA_ALG_CHACHA20_POLY1305, rsp->nonce,
				  sizeof(rsp->nonce), rsp_header.cloud_public_key, ad_len,
				  (void *)&challenge_response, sizeof(challenge_response),
				  (void *)&rsp->ciphertext, sizeof(rsp->ciphertext), &olen);
	if (status != PSA_SUCCESS) {
		LOG_ERR("Failed to encrypt challenge response (%d)", status);
		/* Don't forward any response on failure */
		net_buf_remove_mem(rsp_buf, sizeof(*rsp));
	}

	/* Clear sensitive information */
	mbedtls_platform_zeroize(&challenge_response, sizeof(challenge_response));

	/* Return the response */
	return rsp_buf;
}
