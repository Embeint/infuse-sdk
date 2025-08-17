/**
 * @file
 * @brief Infuse-IoT wrapper for Xoodyak encryption lirbary
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 * Xoodyak is a family of lightweight cryptographic algorithms
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_CRYPTO_XOODYAK_H_
#define INFUSE_SDK_INCLUDE_INFUSE_CRYPTO_XOODYAK_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief xoodyak API
 * @defgroup xoodyak_apis xoodyak APIs
 * @{
 */

/**
 * @brief Encrypt plaintext with ascon-128
 *
 * @param c Ciphertext (Encrypted output of @a m)
 * @param clen Length of output ciphertext
 * @param m Message (Input payload to encrypt)
 * @param mlen Message length
 * @param ad Associated data
 * @param adlen Associated data length
 * @param tag 16 byte ciphertext tag
 * @param npub 16 byte nonce (Initialisation vector)
 * @param k 16 byte key
 *
 * @retval 0 on success
 */
int xoodyak_aead_encrypt(unsigned char *c, unsigned long long *clen, const unsigned char *m,
			 unsigned long long mlen, const unsigned char *ad, unsigned long long adlen,
			 unsigned char *tag, const unsigned char *npub, const unsigned char *k);

/**
 * @brief Decrypt ciphertext with ascon-128
 *
 * @param m Message (Decrypted output)
 * @param mlen Length of output message
 * @param tag 16 byte ciphertext tag
 * @param c Ciphertext (Encrypted input)
 * @param clen Ciphertext length
 * @param ad Associated data
 * @param adlen Associated data length
 * @param npub 16 byte nonce (Initialisation vector)
 * @param k 16 byte key
 *
 * @retval 0 on success
 * @retval -1 on error
 */
int xoodyak_aead_decrypt(unsigned char *m, unsigned long long *mlen, const unsigned char *tag,
			 const unsigned char *c, unsigned long long clen, const unsigned char *ad,
			 unsigned long long adlen, const unsigned char *npub,
			 const unsigned char *k);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_CRYPTO_XOODYAK_H_ */
