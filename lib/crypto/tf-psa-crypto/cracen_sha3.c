/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * SHA3-224, SHA3-256, SHA3-384 and SHA3-512 on the CRACEN CryptoMaster (BA418 engine).
 *
 * The BA418 does not pad messages, the FIPS 202 padding is always appended in software.
 * A single operation is one DMA job:
 *   in:  config, message, padding
 *   out: digest
 *
 * Multi-part operations (see cracen_hash.c) export the 1600 bit Keccak state after each job,
 * and load it again at the start of the next job:
 *   in:  config, [state], message blocks
 *   out: state
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>

#include <psa/crypto.h>

#include "cracen_cm.h"
#include "cracen_hash.h"

/*
 * BA418 (SHA-3) engine values. These are not documented by nrfx or the MDK, and were derived
 * from the nRF Connect SDK CRACEN driver (sxsymcrypt cmdma.h and sha3.c).
 */
#define BA418_TAG_ENGINE          0x5
#define BA418_CONFIG_SHA3_224     6
#define BA418_CONFIG_SHA3_256     7
#define BA418_CONFIG_SHA3_384     11
#define BA418_CONFIG_SHA3_512     15
/* Export the Keccak state at the end of the job, instead of the digest */
#define BA418_CONFIG_SAVE_CONTEXT BIT(6)
/* Size of the Keccak state */
#define BA418_STATE_SZ            200
/* FIPS 202 SHA-3 domain separation and padding bytes */
#define SHA3_PAD_FIRST            0x06
#define SHA3_PAD_LAST             0x80

#define BA418_ENGINE_MASK CRACENCORE_CRYPTMSTRHW_INCLIPSHWCFG_BA418SHA3INCLUDED_Msk
#define BA418_TAG_CONFIG  (BA418_TAG_ENGINE | NRF_CRACEN_CM_DMA_TAG_CONFIG)
#define BA418_TAG_MESSAGE (BA418_TAG_ENGINE | NRF_CRACEN_CM_DMA_TAG_DATATYPE_HASH_MESSAGE)
#define BA418_TAG_STATE                                                                            \
	(BA418_TAG_ENGINE | NRF_CRACEN_CM_DMA_TAG_DATATYPE_HASH_INIT_DATA |                        \
	 NRF_CRACEN_CM_DMA_TAG_LAST)

enum {
	ALG_SHA3_224,
	ALG_SHA3_256,
	ALG_SHA3_384,
	ALG_SHA3_512,
};

/* BA418 algorithm selection */
static const uint8_t modes[] = {
	[ALG_SHA3_224] = BA418_CONFIG_SHA3_224,
	[ALG_SHA3_256] = BA418_CONFIG_SHA3_256,
	[ALG_SHA3_384] = BA418_CONFIG_SHA3_384,
	[ALG_SHA3_512] = BA418_CONFIG_SHA3_512,
};

/* Rate (block size) in bytes */
static const uint8_t rates[] = {
	[ALG_SHA3_224] = 144,
	[ALG_SHA3_256] = 136,
	[ALG_SHA3_384] = 104,
	[ALG_SHA3_512] = 72,
};

static const psa_algorithm_t psa_algs[] = {
	[ALG_SHA3_224] = PSA_ALG_SHA3_224,
	[ALG_SHA3_256] = PSA_ALG_SHA3_256,
	[ALG_SHA3_384] = PSA_ALG_SHA3_384,
	[ALG_SHA3_512] = PSA_ALG_SHA3_512,
};

static int sha3_alg_index(psa_algorithm_t alg)
{
	switch (alg) {
	case PSA_ALG_SHA3_224:
		return IS_ENABLED(CONFIG_TF_PSA_CRYPTO_CRACEN_SHA3_224) ? ALG_SHA3_224 : -1;
	case PSA_ALG_SHA3_256:
		return IS_ENABLED(CONFIG_TF_PSA_CRYPTO_CRACEN_SHA3_256) ? ALG_SHA3_256 : -1;
	case PSA_ALG_SHA3_384:
		return IS_ENABLED(CONFIG_TF_PSA_CRYPTO_CRACEN_SHA3_384) ? ALG_SHA3_384 : -1;
	case PSA_ALG_SHA3_512:
		return IS_ENABLED(CONFIG_TF_PSA_CRYPTO_CRACEN_SHA3_512) ? ALG_SHA3_512 : -1;
	default:
		return -1;
	}
}

/* FIPS 202 padding for a message of @a total bytes, returns padding length (1 to rate bytes) */
static size_t sha3_pad(int idx, uint64_t total, uint8_t *padding)
{
	size_t q = rates[idx] - (total % rates[idx]);

	memset(padding, 0, q);
	padding[0] = SHA3_PAD_FIRST;
	padding[q - 1] |= SHA3_PAD_LAST;
	return q;
}

/**
 * Run a BA418 job
 *
 * @param idx Algorithm index
 * @param cfg Configuration flags (in addition to the algorithm selection)
 * @param state_in Keccak state to resume from, or NULL
 * @param d1 First message buffer
 * @param l1 Length of @a d1
 * @param d2 Second message buffer
 * @param l2 Length of @a d2
 * @param out State or digest output
 * @param out_len Length of @a out
 *
 * The total message length is always a multiple of the rate.
 */
static int sha3_job(int idx, uint32_t cfg, const uint32_t *state_in, const uint8_t *d1, size_t l1,
		    const uint8_t *d2, size_t l2, void *out, size_t out_len)
{
	/* DMA source must be in RAM */
	uint32_t cfg_word = modes[idx] | cfg;
	nrf_cracen_cm_dma_desc_t in[4];
	nrf_cracen_cm_dma_desc_t output;
	int n_in = 0;

	in[n_in++] =
		cracen_cm_desc(&cfg_word, sizeof(cfg_word) | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
			       BA418_TAG_CONFIG);
	if (state_in != NULL) {
		in[n_in++] = cracen_cm_desc(state_in,
					    BA418_STATE_SZ | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
					    BA418_TAG_STATE);
	}
	/* Message descriptors are not realigned, so the data is contiguous */
	if (l1) {
		in[n_in++] = cracen_cm_desc(d1, l1, BA418_TAG_MESSAGE);
	}
	if (l2) {
		in[n_in++] = cracen_cm_desc(d2, l2, BA418_TAG_MESSAGE);
	}
	in[n_in - 1].length |= NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN;
	in[n_in - 1].dmatag |= NRF_CRACEN_CM_DMA_TAG_LAST;

	output = cracen_cm_desc(out, out_len | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
				NRF_CRACEN_CM_DMA_TAG_LAST);

	cracen_cm_chain(in, n_in);
	cracen_cm_chain(&output, 1);
	return cracen_cm_run(in, &output, BA418_ENGINE_MASK);
}

static int sha3_oneshot(int idx, const uint8_t *input, size_t len, uint8_t *digest)
{
	uint8_t padding[CRACEN_HASH_MAX_BLOCK_SZ];
	size_t pad_len = sha3_pad(idx, len, padding);

	return sha3_job(idx, 0, NULL, input, len, padding, pad_len, digest,
			PSA_HASH_LENGTH(psa_algs[idx]));
}

static int sha3_blocks(struct cracen_hash_ctx *ctx, const uint8_t *input, size_t len)
{
	return sha3_job(ctx->alg, BA418_CONFIG_SAVE_CONTEXT, ctx->resume ? ctx->state : NULL,
			ctx->buf, rates[ctx->alg], input, len, ctx->state, BA418_STATE_SZ);
}

static int sha3_finish(struct cracen_hash_ctx *ctx, uint8_t *digest)
{
	uint8_t padding[CRACEN_HASH_MAX_BLOCK_SZ];
	size_t pad_len = sha3_pad(ctx->alg, ctx->total, padding);

	return sha3_job(ctx->alg, 0, ctx->resume ? ctx->state : NULL, ctx->buf, ctx->buf_len,
			padding, pad_len, digest, PSA_HASH_LENGTH(psa_algs[ctx->alg]));
}

const struct cracen_hash_driver cracen_sha3_driver = {
	.engine_mask = BA418_ENGINE_MASK,
	.block_sz = rates,
	.alg_index = sha3_alg_index,
	.oneshot = sha3_oneshot,
	.blocks = sha3_blocks,
	.finish = sha3_finish,
};
