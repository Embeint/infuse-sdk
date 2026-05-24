/* Copyright Embeint Inc
 * SPDX-License-Identifier: Apache-2.0
 *
 * File must declare the following structs:
 *    struct psa_driver_external_integration_hash_operation_t
 */

#ifndef PSA_CRYPTO_DRIVER_EXTERNAL_INTEGRATION_PRIMITIVES_H
#define PSA_CRYPTO_DRIVER_EXTERNAL_INTEGRATION_PRIMITIVES_H

#include "ocrypto_sha1.h"
#include "ocrypto_sha224.h"
#include "ocrypto_sha256.h"
#include "ocrypto_sha384.h"
#include "ocrypto_sha512.h"

struct psa_driver_external_integration_hash_operation {
	psa_algorithm_t alg;
	union {
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
	};
};

typedef struct psa_driver_external_integration_hash_operation
	psa_driver_external_integration_hash_operation_t;

#endif /* PSA_CRYPTO_DRIVER_EXTERNAL_INTEGRATION_PRIMITIVES_H */
