/**
 * @file
 * @brief
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_RPC_COMMANDS_SECURITY_STATE_H_
#define INFUSE_SDK_INCLUDE_INFUSE_RPC_COMMANDS_SECURITY_STATE_H_

#include <stdint.h>

#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	CHALLENGE_RESPONSE_PRE_SHARED_SECRET = 0,
	CHALLENGE_RESPONSE_NORDIC_IDENTITY,
};

struct security_state_response_pss {
	uint8_t challenge[16];
	uint8_t identity_secret[16];
	uint64_t device_id;
} __packed;

struct security_state_response_pss_encrypted {
	uint8_t nonce[12];
	struct {
		struct security_state_response_pss data;
		uint8_t tag[16];
	} ciphertext;
} __packed;

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_RPC_COMMANDS_SECURITY_STATE_H_ */
