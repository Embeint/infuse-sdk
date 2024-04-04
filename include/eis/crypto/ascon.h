/**
 * @file
 * @brief EIS wrapper for ASCON encryption library
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 *
 * @details
 * Ascon is a family of lightweight cryptographic algorithms and consists of:
 *  - Authenticated encryption schemes with associated data (AEAD)
 *  - Hash functions (HASH) and extendible output functions (XOF)
 *  - Pseudo-random functions (PRF) and message authentication codes (MAC)
 *
 * For more details see https://ascon.iaik.tugraz.at/
 */

#ifndef EMBEINT_SDK_INCLUDE_EIS_CRYPTO_ASCON_H_
#define EMBEINT_SDK_INCLUDE_EIS_CRYPTO_ASCON_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ascon API
 * @defgroup ascon_apis ascon APIs
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
 * @param nsec Secret nonce (Unused)
 * @param npub 16 byte nonce (Initialisation vector)
 * @param k 16 byte key
 *
 * @retval 0 on success
 */
int ascon128_aead_encrypt(unsigned char *c, unsigned long long *clen, const unsigned char *m, unsigned long long mlen,
			  const unsigned char *ad, unsigned long long adlen, const unsigned char *nsec,
			  const unsigned char *npub, const unsigned char *k);

/**
 * @brief Decrypt ciphertext with ascon-128
 *
 * @param m Message (Decrypted output)
 * @param mlen Length of output message
 * @param nsec Secret nonce (Unused)
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
int ascon128_aead_decrypt(unsigned char *m, unsigned long long *mlen, unsigned char *nsec, const unsigned char *c,
			  unsigned long long clen, const unsigned char *ad, unsigned long long adlen,
			  const unsigned char *npub, const unsigned char *k);

/**
 * @brief Encrypt plaintext with ascon-128a
 *
 * @param c Ciphertext (Encrypted output of @a m)
 * @param clen Length of output ciphertext
 * @param m Message (Input payload to encrypt)
 * @param mlen Message length
 * @param ad Associated data
 * @param adlen Associated data length
 * @param nsec Secret nonce (Unused)
 * @param npub 16 byte nonce (Initialisation vector)
 * @param k 16 byte key
 *
 * @retval 0 on success
 */
int ascon128a_aead_encrypt(unsigned char *c, unsigned long long *clen, const unsigned char *m, unsigned long long mlen,
			   const unsigned char *ad, unsigned long long adlen, const unsigned char *nsec,
			   const unsigned char *npub, const unsigned char *k);

/**
 * @brief Decrypt ciphertext with ascon-128a
 *
 * @param m Message (Decrypted output)
 * @param mlen Length of output message
 * @param nsec Secret nonce (Unused)
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
int ascon128a_aead_decrypt(unsigned char *m, unsigned long long *mlen, unsigned char *nsec, const unsigned char *c,
			   unsigned long long clen, const unsigned char *ad, unsigned long long adlen,
			   const unsigned char *npub, const unsigned char *k);

/**
 * @brief Encrypt plaintext with ascon-80pq
 *
 * @param c Ciphertext (Encrypted output of @a m)
 * @param clen Length of output ciphertext
 * @param m Message (Input payload to encrypt)
 * @param mlen Message length
 * @param ad Associated data
 * @param adlen Associated data length
 * @param nsec Secret nonce (Unused)
 * @param npub 16 byte nonce (Initialisation vector)
 * @param k 16 byte key
 *
 * @retval 0 on success
 */
int ascon80pq_aead_encrypt(unsigned char *c, unsigned long long *clen, const unsigned char *m, unsigned long long mlen,
			   const unsigned char *ad, unsigned long long adlen, const unsigned char *nsec,
			   const unsigned char *npub, const unsigned char *k);

/**
 * @brief Decrypt ciphertext with ascon-80pq
 *
 * @param m Message (Decrypted output)
 * @param mlen Length of output message
 * @param nsec Secret nonce (Unused)
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
int ascon80pq_aead_decrypt(unsigned char *m, unsigned long long *mlen, unsigned char *nsec, const unsigned char *c,
			   unsigned long long clen, const unsigned char *ad, unsigned long long adlen,
			   const unsigned char *npub, const unsigned char *k);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* EMBEINT_SDK_INCLUDE_EIS_CRYPTO_ASCON_H_ */
