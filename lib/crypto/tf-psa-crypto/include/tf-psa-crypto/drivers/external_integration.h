/* Copyright Embeint Inc
 * SPDX-License-Identifier: Apache-2.0
 *
 * External hooks for cryptographic accelerator integration.
 * All functions must be implemented, even if they only return PSA_ERROR_NOT_SUPPORTED
 */

#ifndef PSA_CRYPTO_DRIVER_EXTERNAL_INTEGRATION_H
#define PSA_CRYPTO_DRIVER_EXTERNAL_INTEGRATION_H

#include <psa/crypto.h>

#include <psa_external_integration_primitives.h>

/**
 * @brief Initialise external driver integration
 *
 * @return PSA_SUCCESS On success, other value on failure
 */
psa_status_t psa_driver_external_integration_init(void);

/**
 * @brief Free resources initialised in @ref psa_driver_external_integration_init
 */
void psa_driver_external_integration_free(void);

/**
 * @brief Sign a message in a single operation
 *
 * @retval PSA_SUCCESS On success
 * @retval PSA_ERROR_NOT_SUPPORTED If algorithm not supported by driver
 * @retval PSA_ERROR_* Other error codes on failure
 */
psa_status_t psa_driver_external_integration_sign_message(
	const psa_key_attributes_t *attributes, const uint8_t *key_buffer, size_t key_buffer_size,
	psa_algorithm_t alg, const uint8_t *input, size_t input_length, uint8_t *signature,
	size_t signature_size, size_t *signature_length);

/**
 * @brief Verify a signed message in a single operation
 *
 * @retval PSA_SUCCESS On success
 * @retval PSA_ERROR_NOT_SUPPORTED If algorithm not supported by driver
 * @retval PSA_ERROR_* Other error codes on failure
 */
psa_status_t psa_driver_external_integration_verify_message(
	const psa_key_attributes_t *attributes, const uint8_t *key_buffer, size_t key_buffer_size,
	psa_algorithm_t alg, const uint8_t *input, size_t input_length, const uint8_t *signature,
	size_t signature_length);

/**
 * @brief Sign a message hash in a single operation
 *
 * @retval PSA_SUCCESS On success
 * @retval PSA_ERROR_NOT_SUPPORTED If algorithm not supported by driver
 * @retval PSA_ERROR_* Other error codes on failure
 */
psa_status_t psa_driver_external_integration_sign_hash(const psa_key_attributes_t *attributes,
						       const uint8_t *key_buffer,
						       size_t key_buffer_size, psa_algorithm_t alg,
						       const uint8_t *hash, size_t hash_length,
						       uint8_t *signature, size_t signature_size,
						       size_t *signature_length);

/**
 * @brief Verify a message hash in a single operation
 *
 * @retval PSA_SUCCESS On success
 * @retval PSA_ERROR_NOT_SUPPORTED If algorithm not supported by driver
 * @retval PSA_ERROR_* Other error codes on failure
 */
psa_status_t psa_driver_external_integration_verify_hash(
	const psa_key_attributes_t *attributes, const uint8_t *key_buffer, size_t key_buffer_size,
	psa_algorithm_t alg, const uint8_t *hash, size_t hash_length, const uint8_t *signature,
	size_t signature_length);

/**
 * @brief Encrypt a buffer in a single operation
 *
 * @retval PSA_SUCCESS On success
 * @retval PSA_ERROR_NOT_SUPPORTED If algorithm not supported by driver
 * @retval PSA_ERROR_* Other error codes on failure
 */
psa_status_t psa_driver_external_integration_cipher_encrypt(
	const psa_key_attributes_t *attributes, const uint8_t *key_buffer, size_t key_buffer_size,
	psa_algorithm_t alg, const uint8_t *iv, size_t iv_length, const uint8_t *input,
	size_t input_length, uint8_t *output, size_t output_size, size_t *output_length);

/**
 * @brief Decrypt a buffer in a single operation
 *
 * @retval PSA_SUCCESS On success
 * @retval PSA_ERROR_NOT_SUPPORTED If algorithm not supported by driver
 * @retval PSA_ERROR_* Other error codes on failure
 */
psa_status_t psa_driver_external_integration_cipher_decrypt(
	const psa_key_attributes_t *attributes, const uint8_t *key_buffer, size_t key_buffer_size,
	psa_algorithm_t alg, const uint8_t *input, size_t input_length, uint8_t *output,
	size_t output_size, size_t *output_length);

/**
 * @brief Compute a hash in a single operation
 *
 * @retval PSA_SUCCESS On success
 * @retval PSA_ERROR_NOT_SUPPORTED If algorithm not supported by driver
 * @retval PSA_ERROR_* Other error codes on failure
 */
psa_status_t psa_driver_external_integration_hash_compute(psa_algorithm_t alg, const uint8_t *input,
							  size_t input_length, uint8_t *hash,
							  size_t hash_size, size_t *hash_length);

/**
 * @brief Setup a multi-part hash operation
 *
 * @retval PSA_SUCCESS On success
 * @retval PSA_ERROR_NOT_SUPPORTED If algorithm not supported by driver
 * @retval PSA_ERROR_* Other error codes on failure
 */
psa_status_t
psa_driver_external_integration_hash_setup(psa_driver_external_integration_hash_operation_t *ctx,
					   psa_algorithm_t alg);

/**
 * @brief Update a multi-part hash operation
 *
 * @retval PSA_SUCCESS On success
 * @retval PSA_ERROR_NOT_SUPPORTED If algorithm not supported by driver
 * @retval PSA_ERROR_* Other error codes on failure
 */
psa_status_t
psa_driver_external_integration_hash_update(psa_driver_external_integration_hash_operation_t *ctx,
					    const uint8_t *input, size_t input_length);

/**
 * @brief Finish a multi-part hash operation
 *
 * @retval PSA_SUCCESS On success
 * @retval PSA_ERROR_NOT_SUPPORTED If algorithm not supported by driver
 * @retval PSA_ERROR_* Other error codes on failure
 */
psa_status_t
psa_driver_external_integration_hash_finish(psa_driver_external_integration_hash_operation_t *ctx,
					    uint8_t *hash, size_t hash_size, size_t *hash_length);

/**
 * @brief AEAD encrypt a buffer in a single operation
 *
 * @retval PSA_SUCCESS On success
 * @retval PSA_ERROR_NOT_SUPPORTED If algorithm not supported by driver
 * @retval PSA_ERROR_* Other error codes on failure
 */
psa_status_t psa_driver_external_integration_aead_encrypt(
	const psa_key_attributes_t *attributes, const uint8_t *key_buffer, size_t key_buffer_size,
	psa_algorithm_t alg, const uint8_t *nonce, size_t nonce_length,
	const uint8_t *additional_data, size_t additional_data_length, const uint8_t *plaintext,
	size_t plaintext_length, uint8_t *ciphertext, size_t ciphertext_size,
	size_t *ciphertext_length);

/**
 * @brief AEAD decrypt a buffer in a single operation
 *
 * @retval PSA_SUCCESS On success
 * @retval PSA_ERROR_NOT_SUPPORTED If algorithm not supported by driver
 * @retval PSA_ERROR_* Other error codes on failure
 */
psa_status_t psa_driver_external_integration_aead_decrypt(
	const psa_key_attributes_t *attributes, const uint8_t *key_buffer, size_t key_buffer_size,
	psa_algorithm_t alg, const uint8_t *nonce, size_t nonce_length,
	const uint8_t *additional_data, size_t additional_data_length, const uint8_t *ciphertext,
	size_t ciphertext_length, uint8_t *plaintext, size_t plaintext_size,
	size_t *plaintext_length);

/**
 * @brief MAC compute in a single operation
 *
 * @retval PSA_SUCCESS On success
 * @retval PSA_ERROR_NOT_SUPPORTED If algorithm not supported by driver
 * @retval PSA_ERROR_* Other error codes on failure
 */
psa_status_t psa_driver_external_integration_mac_compute(const psa_key_attributes_t *attributes,
							 const uint8_t *key_buffer,
							 size_t key_buffer_size,
							 psa_algorithm_t alg, const uint8_t *input,
							 size_t input_length, uint8_t *mac,
							 size_t mac_size, size_t *mac_length);

/**
 * @brief Asymmetric encrypt in a single operation
 *
 * @retval PSA_SUCCESS On success
 * @retval PSA_ERROR_NOT_SUPPORTED If algorithm not supported by driver
 * @retval PSA_ERROR_* Other error codes on failure
 */
psa_status_t psa_driver_external_integration_asymmetric_encrypt(
	const psa_key_attributes_t *attributes, const uint8_t *key_buffer, size_t key_buffer_size,
	psa_algorithm_t alg, const uint8_t *input, size_t input_length, const uint8_t *salt,
	size_t salt_length, uint8_t *output, size_t output_size, size_t *output_length);

/**
 * @brief Asymmetric decrypt in a single operation
 *
 * @retval PSA_SUCCESS On success
 * @retval PSA_ERROR_NOT_SUPPORTED If algorithm not supported by driver
 * @retval PSA_ERROR_* Other error codes on failure
 */
psa_status_t psa_driver_external_integration_asymmetric_decrypt(
	const psa_key_attributes_t *attributes, const uint8_t *key_buffer, size_t key_buffer_size,
	psa_algorithm_t alg, const uint8_t *input, size_t input_length, const uint8_t *salt,
	size_t salt_length, uint8_t *output, size_t output_size, size_t *output_length);

/**
 * @brief Key agreement derivation in a single operation
 *
 * @retval PSA_SUCCESS On success
 * @retval PSA_ERROR_NOT_SUPPORTED If algorithm not supported by driver
 * @retval PSA_ERROR_* Other error codes on failure
 */
psa_status_t psa_driver_external_integration_key_agreement(
	const psa_key_attributes_t *attributes, const uint8_t *key_buffer, size_t key_buffer_size,
	psa_algorithm_t alg, const uint8_t *peer_key, size_t peer_key_length,
	uint8_t *shared_secret, size_t shared_secret_size, size_t *shared_secret_length);

#endif /* PSA_CRYPTO_DRIVER_EXTERNAL_INTEGRATION_H */
