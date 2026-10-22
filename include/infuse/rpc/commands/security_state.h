/**
 * @file
 * @brief security_state RPC response definitions
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_RPC_COMMANDS_SECURITY_STATE_H_
#define INFUSE_SDK_INCLUDE_INFUSE_RPC_COMMANDS_SECURITY_STATE_H_

#include <stdint.h>

#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup rpc_commands_apis
 * @{
 */

enum {
	CHALLENGE_RESPONSE_HARDWARE_ID = 0,
	CHALLENGE_RESPONSE_NORDIC_IDENTITY,
};

/** @brief Hardware ID challenge response (plaintext) */
struct security_state_response_hw_id {
	/** Challenge bytes echoed from the request */
	uint8_t challenge[16];
	/** Hardware ID from hwinfo, zero padded */
	uint8_t hardware_id[16];
	/** Infuse-IoT device ID */
	uint64_t device_id;
} __packed;

/** @brief Hardware ID challenge response, encrypted with ChaCha20-Poly1305 */
struct security_state_response_hw_id_encrypted {
	/** Encryption nonce */
	uint8_t nonce[12];
	/** Encrypted payload */
	struct {
		/** Encrypted @ref security_state_response_hw_id */
		struct security_state_response_hw_id data;
		/** Authentication tag */
		uint8_t tag[16];
	} __packed ciphertext;
} __packed;

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_RPC_COMMANDS_SECURITY_STATE_H_ */
