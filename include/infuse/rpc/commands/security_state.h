/**
 * @file
 * @brief
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

enum {
	CHALLENGE_RESPONSE_HARDWARE_ID = 0,
	CHALLENGE_RESPONSE_NORDIC_IDENTITY,
};

struct security_state_response_hw_id {
	uint8_t challenge[16];
	uint8_t hardware_id[16];
	uint64_t device_id;
} __packed;

struct security_state_response_hw_id_encrypted {
	uint8_t nonce[12];
	struct {
		struct security_state_response_hw_id data;
		uint8_t tag[16];
	} ciphertext;
} __packed;

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_RPC_COMMANDS_SECURITY_STATE_H_ */
