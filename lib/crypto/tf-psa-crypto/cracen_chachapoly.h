/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_LIB_CRYPTO_TF_PSA_CRYPTO_CRACEN_CHACHAPOLY_H_
#define INFUSE_LIB_CRYPTO_TF_PSA_CRYPTO_CRACEN_CHACHAPOLY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Check whether an operation can be run on the CRACEN
 *
 * @retval true Operation can be run on the CRACEN
 * @retval false Operation must use a software implementation
 */
bool cracen_chachapoly_supported(const uint8_t *key, const uint8_t *nonce, size_t nonce_len,
				 const uint8_t *aad, size_t aad_len, const uint8_t *input,
				 const uint8_t *output, size_t len);

/**
 * @brief One-shot ChaCha20-Poly1305 on the CRACEN CryptoMaster
 *
 * @param decrypt Decrypt @a input instead of encrypting it
 * @param key 256 bit key
 * @param nonce 96 bit nonce
 * @param aad Additional authenticated data
 * @param aad_len Length of @a aad
 * @param input Plaintext (encrypt) or ciphertext (decrypt)
 * @param len Length of @a input and @a output
 * @param output Ciphertext (encrypt) or plaintext (decrypt)
 * @param tag Computed authentication tag (to be compared by caller on decrypt)
 *
 * @retval 0 on success
 * @retval -ENOTSUP operation not supported, no buffers have been modified
 * @retval -EIO on DMA failure, @a output may have been modified
 */
int cracen_chachapoly_crypt(bool decrypt, const uint8_t key[32], const uint8_t nonce[12],
			    const uint8_t *aad, size_t aad_len, const uint8_t *input, size_t len,
			    uint8_t *output, uint8_t tag[16]);

#endif /* INFUSE_LIB_CRYPTO_TF_PSA_CRYPTO_CRACEN_CHACHAPOLY_H_ */
