/* Copyright Embeint Inc
 * SPDX-License-Identifier: Apache-2.0
 *
 * File must declare the following structs:
 *    struct psa_driver_external_integration_hash_operation_t
 */

#ifndef PSA_CRYPTO_DRIVER_EXTERNAL_INTEGRATION_PRIMITIVES_H
#define PSA_CRYPTO_DRIVER_EXTERNAL_INTEGRATION_PRIMITIVES_H

#include <stdbool.h>
#include <stdint.h>

#include "ocrypto_sha1.h"
#include "ocrypto_sha224.h"
#include "ocrypto_sha256.h"
#include "ocrypto_sha384.h"
#include "ocrypto_sha512.h"

#ifdef CONFIG_TF_PSA_CRYPTO_CRACEN_HASH
#if defined(CONFIG_TF_PSA_CRYPTO_CRACEN_SHA3)
/* Keccak state, SHA3-224 rate */
#define CRACEN_HASH_MAX_STATE_SZ 200
#define CRACEN_HASH_MAX_BLOCK_SZ 144
#elif defined(CONFIG_TF_PSA_CRYPTO_CRACEN_SHA384) || defined(CONFIG_TF_PSA_CRYPTO_CRACEN_SHA512)
#define CRACEN_HASH_MAX_STATE_SZ 64
#define CRACEN_HASH_MAX_BLOCK_SZ 128
#else
#define CRACEN_HASH_MAX_STATE_SZ 32
#define CRACEN_HASH_MAX_BLOCK_SZ 64
#endif

struct cracen_hash_driver;

/** CRACEN hash multi-part operation state */
struct cracen_hash_ctx {
	/** Engine state after the processed blocks (DMA source and destination) */
	uint32_t state[CRACEN_HASH_MAX_STATE_SZ / sizeof(uint32_t)];
	/** Pending input, 1 to block size bytes once any data has been provided */
	uint8_t buf[CRACEN_HASH_MAX_BLOCK_SZ];
	/** Total number of bytes provided */
	uint64_t total;
	/** Engine driver */
	const struct cracen_hash_driver *drv;
	/** Number of bytes in @a buf */
	uint8_t buf_len;
	/** Driver algorithm index */
	uint8_t alg;
	/** @a state is valid (at least one block has been processed) */
	bool resume;
};
#endif /* CONFIG_TF_PSA_CRYPTO_CRACEN_HASH */

struct psa_driver_external_integration_hash_operation {
	psa_algorithm_t alg;
	union {
		/* Ensures the union is never empty, no larger than any other member */
		uint32_t unused;
#ifdef CONFIG_TF_PSA_CRYPTO_CRACEN_HASH
		struct cracen_hash_ctx cracen;
#endif
#ifdef CONFIG_TF_PSA_CRYPTO_OBERON_ACCELERATOR_SHA
#ifdef PSA_WANT_ALG_SHA_1
		ocrypto_sha1_ctx sha1_ctx;
#endif
#ifdef PSA_WANT_ALG_SHA_224
		ocrypto_sha224_ctx sha224_ctx;
#endif
#ifdef PSA_WANT_ALG_SHA_256
		ocrypto_sha256_ctx sha256_ctx;
#endif
#ifdef PSA_WANT_ALG_SHA_384
		ocrypto_sha384_ctx sha384_ctx;
#endif
#ifdef PSA_WANT_ALG_SHA_512
		ocrypto_sha512_ctx sha512_ctx;
#endif
#endif /* CONFIG_TF_PSA_CRYPTO_OBERON_ACCELERATOR_SHA */
	};
};

typedef struct psa_driver_external_integration_hash_operation
	psa_driver_external_integration_hash_operation_t;

#endif /* PSA_CRYPTO_DRIVER_EXTERNAL_INTEGRATION_PRIMITIVES_H */
