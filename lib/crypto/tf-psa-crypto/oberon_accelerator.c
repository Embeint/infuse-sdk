/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>

#include <tf-psa-crypto/drivers/external_integration.h>

#include "ocrypto_chacha20_poly1305.h"
#include "ocrypto_curve25519.h"
#include "ocrypto_sha1.h"
#include "ocrypto_sha224.h"
#include "ocrypto_sha256.h"
#include "ocrypto_sha384.h"
#include "ocrypto_sha512.h"

psa_status_t psa_driver_external_integration_init(void)
{
	return PSA_SUCCESS;
}

void psa_driver_external_integration_free(void)
{
}

psa_status_t psa_driver_external_integration_sign_message(
	const psa_key_attributes_t *attributes, const uint8_t *key_buffer, size_t key_buffer_size,
	psa_algorithm_t alg, const uint8_t *input, size_t input_length, uint8_t *signature,
	size_t signature_size, size_t *signature_length)
{
	return PSA_ERROR_NOT_SUPPORTED;
}

psa_status_t psa_driver_external_integration_verify_message(
	const psa_key_attributes_t *attributes, const uint8_t *key_buffer, size_t key_buffer_size,
	psa_algorithm_t alg, const uint8_t *input, size_t input_length, const uint8_t *signature,
	size_t signature_length)
{
	return PSA_ERROR_NOT_SUPPORTED;
}

psa_status_t psa_driver_external_integration_sign_hash(const psa_key_attributes_t *attributes,
						       const uint8_t *key_buffer,
						       size_t key_buffer_size, psa_algorithm_t alg,
						       const uint8_t *hash, size_t hash_length,
						       uint8_t *signature, size_t signature_size,
						       size_t *signature_length)
{
	return PSA_ERROR_NOT_SUPPORTED;
}

psa_status_t psa_driver_external_integration_verify_hash(
	const psa_key_attributes_t *attributes, const uint8_t *key_buffer, size_t key_buffer_size,
	psa_algorithm_t alg, const uint8_t *hash, size_t hash_length, const uint8_t *signature,
	size_t signature_length)
{
	return PSA_ERROR_NOT_SUPPORTED;
}

psa_status_t psa_driver_external_integration_cipher_encrypt(
	const psa_key_attributes_t *attributes, const uint8_t *key_buffer, size_t key_buffer_size,
	psa_algorithm_t alg, const uint8_t *iv, size_t iv_length, const uint8_t *input,
	size_t input_length, uint8_t *output, size_t output_size, size_t *output_length)
{
	return PSA_ERROR_NOT_SUPPORTED;
}

psa_status_t psa_driver_external_integration_cipher_decrypt(
	const psa_key_attributes_t *attributes, const uint8_t *key_buffer, size_t key_buffer_size,
	psa_algorithm_t alg, const uint8_t *input, size_t input_length, uint8_t *output,
	size_t output_size, size_t *output_length)
{
	return PSA_ERROR_NOT_SUPPORTED;
}

psa_status_t psa_driver_external_integration_hash_compute(psa_algorithm_t alg, const uint8_t *input,
							  size_t input_length, uint8_t *hash,
							  size_t hash_size, size_t *hash_length)
{
	switch (alg) {
#ifdef CONFIG_TF_PSA_CRYPTO_OBERON_ACCELERATOR_SHA
#ifdef CONFIG_PSA_WANT_ALG_SHA_1
	case PSA_ALG_SHA_1:
		if (hash_size < ocrypto_sha1_BYTES) {
			return PSA_ERROR_INVALID_ARGUMENT;
		}
		ocrypto_sha1(hash, input, input_length);
		*hash_length = ocrypto_sha1_BYTES;
		break;
#endif /* CONFIG_PSA_WANT_ALG_SHA_1 */
#ifdef CONFIG_PSA_WANT_ALG_SHA_224
	case PSA_ALG_SHA_224:
		if (hash_size < ocrypto_sha224_BYTES) {
			return PSA_ERROR_INVALID_ARGUMENT;
		}
		ocrypto_sha224(hash, input, input_length);
		*hash_length = ocrypto_sha224_BYTES;
		break;
#endif /* CONFIG_PSA_WANT_ALG_SHA_224 */
#ifdef CONFIG_PSA_WANT_ALG_SHA_256
	case PSA_ALG_SHA_256:
		if (hash_size < ocrypto_sha256_BYTES) {
			return PSA_ERROR_INVALID_ARGUMENT;
		}
		ocrypto_sha256(hash, input, input_length);
		*hash_length = ocrypto_sha256_BYTES;
		break;
#endif /* CONFIG_PSA_WANT_ALG_SHA_256 */
#ifdef CONFIG_PSA_WANT_ALG_SHA_384
	case PSA_ALG_SHA_384:
		if (hash_size < ocrypto_sha384_BYTES) {
			return PSA_ERROR_INVALID_ARGUMENT;
		}
		ocrypto_sha384(hash, input, input_length);
		*hash_length = ocrypto_sha384_BYTES;
		break;
#endif /* CONFIG_PSA_WANT_ALG_SHA_384 */
#ifdef CONFIG_PSA_WANT_ALG_SHA_512
	case PSA_ALG_SHA_512:
		if (hash_size < ocrypto_sha512_BYTES) {
			return PSA_ERROR_INVALID_ARGUMENT;
		}
		ocrypto_sha512(hash, input, input_length);
		*hash_length = ocrypto_sha512_BYTES;
		break;
#endif /* CONFIG_PSA_WANT_ALG_SHA_384 */
#endif /* CONFIG_TF_PSA_CRYPTO_OBERON_ACCELERATOR_SHA */
	default:
		return PSA_ERROR_NOT_SUPPORTED;
	}

	return PSA_SUCCESS;
}

psa_status_t
psa_driver_external_integration_hash_setup(psa_driver_external_integration_hash_operation_t *ctx,
					   psa_algorithm_t alg)
{
	switch (alg) {
#ifdef CONFIG_TF_PSA_CRYPTO_OBERON_ACCELERATOR_SHA
#ifdef PSA_WANT_ALG_SHA_1
	case PSA_ALG_SHA_1:
		ocrypto_sha1_init(&ctx->sha1_ctx);
		break;
#endif
#ifdef PSA_WANT_ALG_SHA_224
	case PSA_ALG_SHA_224:
		ocrypto_sha224_init(&ctx->sha224_ctx);
		break;
#endif
#ifdef PSA_WANT_ALG_SHA_256
	case PSA_ALG_SHA_256:
		ocrypto_sha256_init(&ctx->sha256_ctx);
		break;
#endif
#ifdef PSA_WANT_ALG_SHA_384
	case PSA_ALG_SHA_384:
		ocrypto_sha384_init(&ctx->sha384_ctx);
		break;
#endif
#ifdef PSA_WANT_ALG_SHA_512
	case PSA_ALG_SHA_512:
		ocrypto_sha512_init(&ctx->sha512_ctx);
		break;
#endif
#endif /* CONFIG_TF_PSA_CRYPTO_OBERON_ACCELERATOR_SHA */
	default:
		return PSA_ERROR_NOT_SUPPORTED;
	}

	ctx->alg = alg;
	return PSA_SUCCESS;
}

psa_status_t
psa_driver_external_integration_hash_update(psa_driver_external_integration_hash_operation_t *ctx,
					    const uint8_t *input, size_t input_length)
{
	switch (ctx->alg) {
#ifdef CONFIG_TF_PSA_CRYPTO_OBERON_ACCELERATOR_SHA
#ifdef PSA_WANT_ALG_SHA_1
	case PSA_ALG_SHA_1:
		ocrypto_sha1_update(&ctx->sha1_ctx, input, input_length);
		break;
#endif
#ifdef PSA_WANT_ALG_SHA_224
	case PSA_ALG_SHA_224:
		ocrypto_sha224_update(&ctx->sha224_ctx, input, input_length);
		break;
#endif
#ifdef PSA_WANT_ALG_SHA_256
	case PSA_ALG_SHA_256:
		ocrypto_sha256_update(&ctx->sha256_ctx, input, input_length);
		break;
#endif
#ifdef PSA_WANT_ALG_SHA_384
	case PSA_ALG_SHA_384:
		ocrypto_sha384_update(&ctx->sha384_ctx, input, input_length);
		break;
#endif
#ifdef PSA_WANT_ALG_SHA_512
	case PSA_ALG_SHA_512:
		ocrypto_sha512_update(&ctx->sha512_ctx, input, input_length);
		break;
#endif
#endif /* CONFIG_TF_PSA_CRYPTO_OBERON_ACCELERATOR_SHA */
	default:
		return PSA_ERROR_NOT_SUPPORTED;
	}

	return PSA_SUCCESS;
}

psa_status_t
psa_driver_external_integration_hash_finish(psa_driver_external_integration_hash_operation_t *ctx,
					    uint8_t *hash, size_t hash_size, size_t *hash_length)
{
	switch (ctx->alg) {
#ifdef CONFIG_TF_PSA_CRYPTO_OBERON_ACCELERATOR_SHA
#ifdef PSA_WANT_ALG_SHA_1
	case PSA_ALG_SHA_1:
		if (hash_size < ocrypto_sha1_BYTES) {
			return PSA_ERROR_INVALID_ARGUMENT;
		}
		ocrypto_sha1_final(&ctx->sha1_ctx, hash);
		*hash_length = ocrypto_sha1_BYTES;
		break;
#endif
#ifdef PSA_WANT_ALG_SHA_224
	case PSA_ALG_SHA_224:
		if (hash_size < ocrypto_sha224_BYTES) {
			return PSA_ERROR_INVALID_ARGUMENT;
		}
		ocrypto_sha224_final(&ctx->sha224_ctx, hash);
		*hash_length = ocrypto_sha224_BYTES;
		break;
#endif
#ifdef PSA_WANT_ALG_SHA_256
	case PSA_ALG_SHA_256:
		if (hash_size < ocrypto_sha256_BYTES) {
			return PSA_ERROR_INVALID_ARGUMENT;
		}
		ocrypto_sha256_final(&ctx->sha256_ctx, hash);
		*hash_length = ocrypto_sha256_BYTES;
		break;
#endif
#ifdef PSA_WANT_ALG_SHA_384
	case PSA_ALG_SHA_384:
		if (hash_size < ocrypto_sha384_BYTES) {
			return PSA_ERROR_INVALID_ARGUMENT;
		}
		ocrypto_sha384_final(&ctx->sha384_ctx, hash);
		*hash_length = ocrypto_sha384_BYTES;
		break;
#endif
#ifdef PSA_WANT_ALG_SHA_512
	case PSA_ALG_SHA_512:
		if (hash_size < ocrypto_sha512_BYTES) {
			return PSA_ERROR_INVALID_ARGUMENT;
		}
		ocrypto_sha512_final(&ctx->sha512_ctx, hash);
		*hash_length = ocrypto_sha512_BYTES;
		break;
#endif
#endif /* CONFIG_TF_PSA_CRYPTO_OBERON_ACCELERATOR_SHA */
	default:
		return PSA_ERROR_NOT_SUPPORTED;
	}

	return PSA_SUCCESS;
}

psa_status_t psa_driver_external_integration_aead_encrypt(
	const psa_key_attributes_t *attributes, const uint8_t *key_buffer, size_t key_buffer_size,
	psa_algorithm_t alg, const uint8_t *nonce, size_t nonce_length,
	const uint8_t *additional_data, size_t additional_data_length, const uint8_t *plaintext,
	size_t plaintext_length, uint8_t *ciphertext, size_t ciphertext_size,
	size_t *ciphertext_length)
{
#ifdef CONFIG_PSA_WANT_ALG_CHACHA20_POLY1305
	size_t output_size;

	if (alg != PSA_ALG_CHACHA20_POLY1305) {
		return PSA_ERROR_NOT_SUPPORTED;
	}
	if (key_buffer_size != ocrypto_chacha20_poly1305_KEY_BYTES) {
		return PSA_ERROR_NOT_SUPPORTED;
	}
	output_size = plaintext_length + ocrypto_chacha20_poly1305_TAG_BYTES;
	if (ciphertext_size < output_size) {
		return PSA_ERROR_INVALID_ARGUMENT;
	}

	ocrypto_chacha20_poly1305_encrypt(ciphertext + plaintext_length, ciphertext, plaintext,
					  plaintext_length, additional_data, additional_data_length,
					  nonce, nonce_length, key_buffer);
	*ciphertext_length = output_size;
	return PSA_SUCCESS;
#else
	return PSA_ERROR_NOT_SUPPORTED;
#endif /* CONFIG_PSA_WANT_ALG_CHACHA20_POLY1305 */
}

psa_status_t psa_driver_external_integration_aead_decrypt(
	const psa_key_attributes_t *attributes, const uint8_t *key_buffer, size_t key_buffer_size,
	psa_algorithm_t alg, const uint8_t *nonce, size_t nonce_length,
	const uint8_t *additional_data, size_t additional_data_length, const uint8_t *ciphertext,
	size_t ciphertext_length, uint8_t *plaintext, size_t plaintext_size,
	size_t *plaintext_length)
{
#ifdef CONFIG_PSA_WANT_ALG_CHACHA20_POLY1305
	int rc;

	if (alg != PSA_ALG_CHACHA20_POLY1305) {
		return PSA_ERROR_NOT_SUPPORTED;
	}
	if (key_buffer_size != ocrypto_chacha20_poly1305_KEY_BYTES) {
		return PSA_ERROR_NOT_SUPPORTED;
	}

	const size_t ciphertext_no_tag = ciphertext_length - ocrypto_chacha20_poly1305_TAG_BYTES;
	const uint8_t *tag = ciphertext + ciphertext_no_tag;

	if (plaintext_size < ciphertext_no_tag) {
		return PSA_ERROR_INVALID_ARGUMENT;
	}

	rc = ocrypto_chacha20_poly1305_decrypt(tag, plaintext, ciphertext, ciphertext_no_tag,
					       additional_data, additional_data_length, nonce,
					       nonce_length, key_buffer);
	*plaintext_length = ciphertext_no_tag;
	return rc == 0 ? PSA_SUCCESS : PSA_ERROR_INVALID_SIGNATURE;
#else
	return PSA_ERROR_NOT_SUPPORTED;
#endif /* CONFIG_PSA_WANT_ALG_CHACHA20_POLY1305 */
}

psa_status_t psa_driver_external_integration_mac_compute(const psa_key_attributes_t *attributes,
							 const uint8_t *key_buffer,
							 size_t key_buffer_size,
							 psa_algorithm_t alg, const uint8_t *input,
							 size_t input_length, uint8_t *mac,
							 size_t mac_size, size_t *mac_length)
{
	return PSA_ERROR_NOT_SUPPORTED;
}

psa_status_t psa_driver_external_integration_asymmetric_encrypt(
	const psa_key_attributes_t *attributes, const uint8_t *key_buffer, size_t key_buffer_size,
	psa_algorithm_t alg, const uint8_t *input, size_t input_length, const uint8_t *salt,
	size_t salt_length, uint8_t *output, size_t output_size, size_t *output_length)
{
	return PSA_ERROR_NOT_SUPPORTED;
}

psa_status_t psa_driver_external_integration_asymmetric_decrypt(
	const psa_key_attributes_t *attributes, const uint8_t *key_buffer, size_t key_buffer_size,
	psa_algorithm_t alg, const uint8_t *input, size_t input_length, const uint8_t *salt,
	size_t salt_length, uint8_t *output, size_t output_size, size_t *output_length)
{
	return PSA_ERROR_NOT_SUPPORTED;
}

psa_status_t psa_driver_external_integration_key_agreement(
	const psa_key_attributes_t *attributes, const uint8_t *key_buffer, size_t key_buffer_size,
	psa_algorithm_t alg, const uint8_t *peer_key, size_t peer_key_length,
	uint8_t *shared_secret, size_t shared_secret_size, size_t *shared_secret_length)
{
#ifdef CONFIG_TF_PSA_CRYPTO_OBERON_ACCELERATOR_KEY_AGREEMENT
	psa_key_type_t key_type;
	size_t key_bits;

	if (alg != PSA_ALG_ECDH) {
		return PSA_ERROR_NOT_SUPPORTED;
	}
	key_type = psa_get_key_type(attributes);
	if (key_type != PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY)) {
		return PSA_ERROR_NOT_SUPPORTED;
	}
	key_bits = psa_get_key_bits(attributes);
	if (key_bits != 255) {
		return PSA_ERROR_NOT_SUPPORTED;
	}
	if (key_buffer_size != ocrypto_curve25519_SCALAR_BYTES) {
		return PSA_ERROR_INVALID_ARGUMENT;
	}
	if (peer_key_length != ocrypto_curve25519_BYTES) {
		return PSA_ERROR_INVALID_ARGUMENT;
	}
	if (shared_secret_size < ocrypto_curve25519_BYTES) {
		return PSA_ERROR_INVALID_ARGUMENT;
	}

	ocrypto_curve25519_scalarmult(shared_secret, key_buffer, peer_key);
	*shared_secret_length = ocrypto_curve25519_BYTES;
	return PSA_SUCCESS;
#else
	return PSA_ERROR_NOT_SUPPORTED;
#endif /* CONFIG_TF_PSA_CRYPTO_OBERON_ACCELERATOR_KEY_AGREEMENT */
}
