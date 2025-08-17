/**
 * @file
 * @brief Extra MbedTLS requirements for Infuse-IoT
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TFM_MBEDTLS_CONFIG_EXTRA_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TFM_MBEDTLS_CONFIG_EXTRA_H_

/* Infuse-IoT requires the chacha20-poly1305 algorithm */
#define PSA_WANT_KEY_TYPE_CHACHA20     1
#define PSA_WANT_ALG_CHACHA20_POLY1305 1

/* Infuse-IoT requires HKDF */
#define PSA_WANT_ALG_HKDF    1
#define PSA_WANT_ALG_SHA_256 1

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TFM_MBEDTLS_CONFIG_EXTRA_H_ */
