/**
 * @file
 * @brief Additional MbedTLS user config
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_CRYPTO_MBEDTLS_MBEDTLS_USER_CONFIG_H_
#define INFUSE_SDK_INCLUDE_INFUSE_CRYPTO_MBEDTLS_MBEDTLS_USER_CONFIG_H_

#ifdef CONFIG_NRF_OBERON

#error Oberon library does not yet support MbedTLS 4.x

#endif /* CONFIG_NRF_OBERON */

#endif /* INFUSE_SDK_INCLUDE_INFUSE_CRYPTO_MBEDTLS_MBEDTLS_USER_CONFIG_H_ */
