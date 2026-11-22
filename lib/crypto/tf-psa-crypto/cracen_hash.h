/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * Common handling of hash algorithms on the CRACEN CryptoMaster.
 */

#ifndef INFUSE_LIB_CRYPTO_TF_PSA_CRYPTO_CRACEN_HASH_H_
#define INFUSE_LIB_CRYPTO_TF_PSA_CRYPTO_CRACEN_HASH_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <psa/crypto.h>

/* Defined in psa_external_integration_primitives.h */
struct cracen_hash_ctx;

/** Engine specific part of a CRACEN hash implementation */
struct cracen_hash_driver {
	/** CRACENCORE_CRYPTMSTRHW_INCLIPSHWCFG_*_Msk of the engine */
	uint32_t engine_mask;
	/** Block size (rate) of each algorithm index */
	const uint8_t *block_sz;
	/**
	 * Get the algorithm index of a PSA algorithm
	 *
	 * @return Algorithm index, or -1 if the algorithm is not handled by the driver
	 */
	int (*alg_index)(psa_algorithm_t alg);
	/**
	 * Hash a complete message in a single job
	 *
	 * @a input is accessible by the DMA (including the following bytes up to a multiple of 4),
	 * and fits in a single descriptor.
	 */
	int (*oneshot)(int idx, const uint8_t *input, size_t len, uint8_t *digest);
	/**
	 * Process the full block in ctx->buf, followed by @a len bytes of @a input (a multiple of
	 * the block size), exporting the intermediate state to ctx->state.
	 */
	int (*blocks)(struct cracen_hash_ctx *ctx, const uint8_t *input, size_t len);
	/** Process ctx->buf and output the digest of the complete message */
	int (*finish)(struct cracen_hash_ctx *ctx, uint8_t *digest);
};

#ifdef CONFIG_TF_PSA_CRYPTO_CRACEN_SHA2
/** SHA-224, SHA-256, SHA-384 and SHA-512 (BA413 engine) */
extern const struct cracen_hash_driver cracen_sha2_driver;
#endif /* CONFIG_TF_PSA_CRYPTO_CRACEN_SHA2 */
#ifdef CONFIG_TF_PSA_CRYPTO_CRACEN_SHA3
/** SHA3-224, SHA3-256, SHA3-384 and SHA3-512 (BA418 engine) */
extern const struct cracen_hash_driver cracen_sha3_driver;
#endif /* CONFIG_TF_PSA_CRYPTO_CRACEN_SHA3 */

/**
 * @brief Check whether a hash algorithm is accelerated on the CRACEN
 *
 * @param alg PSA hash algorithm
 *
 * @retval true Algorithm is handled by the CRACEN
 * @retval false Algorithm is not handled by the CRACEN
 */
bool cracen_hash_supported(psa_algorithm_t alg);

/**
 * @brief Start a multi-part hash operation on the CRACEN
 *
 * @param ctx Operation context
 * @param alg PSA hash algorithm
 *
 * @retval 0 on success
 * @retval -ENOTSUP The algorithm is not supported, or the hash engine is not present
 */
int cracen_hash_setup(struct cracen_hash_ctx *ctx, psa_algorithm_t alg);

/**
 * @brief Add data to a multi-part hash operation
 *
 * @param ctx Operation context
 * @param input Data to hash (any memory)
 * @param len Length of @a input
 *
 * @retval 0 on success
 * @retval -ENOTSUP The CRACEN could not be acquired (interrupt context with the semaphore)
 * @retval -EIO DMA error
 * @retval -ETIMEDOUT Hardware job did not complete
 */
int cracen_hash_update(struct cracen_hash_ctx *ctx, const uint8_t *input, size_t len);

/**
 * @brief Finish a multi-part hash operation
 *
 * @param ctx Operation context, cleared on return
 * @param digest Output digest, PSA_HASH_LENGTH() of the operation algorithm
 *
 * @retval 0 on success
 * @retval -ENOTSUP The CRACEN could not be acquired (interrupt context with the semaphore)
 * @retval -EIO DMA error
 * @retval -ETIMEDOUT Hardware job did not complete
 */
int cracen_hash_finish(struct cracen_hash_ctx *ctx, uint8_t *digest);

/**
 * @brief Compute a digest in a single operation
 *
 * @param alg PSA hash algorithm
 * @param input Data to hash (any memory)
 * @param len Length of @a input
 * @param digest Output digest, PSA_HASH_LENGTH(@a alg)
 *
 * @retval 0 on success
 * @retval -ENOTSUP The algorithm is not supported, the hash engine is not present, or the CRACEN
 *         could not be acquired (interrupt context with the semaphore). No output has been
 *         written.
 * @retval -EIO DMA error
 * @retval -ETIMEDOUT Hardware job did not complete
 */
int cracen_hash_compute(psa_algorithm_t alg, const uint8_t *input, size_t len, uint8_t *digest);

#endif /* INFUSE_LIB_CRYPTO_TF_PSA_CRYPTO_CRACEN_HASH_H_ */
