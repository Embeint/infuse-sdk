/**
 * @file
 * @brief Extra MbedTLS requirements for EIS
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TFM_MBEDTLS_CONFIG_EXTRA_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TFM_MBEDTLS_CONFIG_EXTRA_H_

/* EIS requires the chacha20-poly1305 algorithm */
#define PSA_WANT_KEY_TYPE_CHACHA20     1
#define PSA_WANT_ALG_CHACHA20_POLY1305 1

/* EIS requires HKDF */
#define PSA_WANT_ALG_HKDF    1
#define PSA_WANT_ALG_SHA_256 1

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TFM_MBEDTLS_CONFIG_EXTRA_H_ */
