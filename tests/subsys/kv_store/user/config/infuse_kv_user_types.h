/**
 * @file
 * @brief
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 *
 * @details
 */

#ifndef INFUSE_SDK_TESTS_SUBSYS_KV_STORE_USER_CONFIG_INFUSE_KV_USER_TYPES_H_
#define INFUSE_SDK_TESTS_SUBSYS_KV_STORE_USER_CONFIG_INFUSE_KV_USER_TYPES_H_

#ifdef __cplusplus
extern "C" {
#endif

struct kv_user_1 {
	struct kv_string value;
} __packed;

/* clang-format off */
/* Compile time definition for known array length */
#define _KV_KEY_USER_1_VAR(num) \
	struct { \
		uint8_t family; \
		KV_STRUCT_KV_STRING_VAR(num) apn; \
	} __packed
/* clang-format on */

struct kv_user_2 {
	uint32_t value_a;
	int16_t value_b;
	uint8_t value_c;
} __packed;

enum kv_user_id {
	KV_KEY_USER_1 = KV_KEY_BUILTIN_END + 10,
	KV_KEY_USER_2 = KV_KEY_BUILTIN_END + 50,
};

enum kv_user_size {
	_KV_KEY_USER_2_SIZE = sizeof(struct kv_user_2),
};

#define _KV_KEY_USER_1_TYPE struct kv_user_1
#define _KV_KEY_USER_2_TYPE struct kv_user_2

#define KV_USER_REFLECT_NUM 2

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_TESTS_SUBSYS_KV_STORE_USER_CONFIG_INFUSE_KV_USER_TYPES_H_ */
