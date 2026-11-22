/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * SHA-224, SHA-256, SHA-384 and SHA-512 on the CRACEN CryptoMaster (BA413 engine).
 *
 * A single operation is one DMA job, with padding inserted by the hardware:
 *   in:  config, message
 *   out: digest
 *
 * Multi-part operations (see cracen_hash.c) export the intermediate hash state after each job,
 * and load it again at the start of the next job:
 *   in:  config, [state], message blocks
 *   out: state
 * The final job of a resumed operation outputs the digest, with the padding computed in software
 * as the hardware only counts the bytes of the current job.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#include <psa/crypto.h>

#include "cracen_cm.h"
#include "cracen_hash.h"

/*
 * BA413 (hash) engine configuration values. These are not documented by nrfx or the MDK, and
 * were derived from the nRF Connect SDK CRACEN driver (sxsymcrypt hash.c).
 */
#define BA413_CONFIG_SHA224 0x04
#define BA413_CONFIG_SHA256 0x08
#define BA413_CONFIG_SHA384 0x10
#define BA413_CONFIG_SHA512 0x20
/* Hardware inserts the padding (only valid when the whole message is in one job) */
#define BA413_CONFIG_HW_PAD BIT(9)
/* Output the final digest, instead of exporting the intermediate state */
#define BA413_CONFIG_FINAL  BIT(10)

#define BA413_ENGINE_MASK CRACENCORE_CRYPTMSTRHW_INCLIPSHWCFG_BA413HASHINCLUDED_Msk
#define BA413_TAG_CONFIG  (NRF_CRACEN_CM_DMA_TAG_ENGINE_HASH | NRF_CRACEN_CM_DMA_TAG_CONFIG)
#define BA413_TAG_MESSAGE                                                                          \
	(NRF_CRACEN_CM_DMA_TAG_ENGINE_HASH | NRF_CRACEN_CM_DMA_TAG_DATATYPE_HASH_MESSAGE)
#define BA413_TAG_STATE                                                                            \
	(NRF_CRACEN_CM_DMA_TAG_ENGINE_HASH | NRF_CRACEN_CM_DMA_TAG_DATATYPE_HASH_INIT_DATA |       \
	 NRF_CRACEN_CM_DMA_TAG_LAST)

struct sha2_alg {
	/* BA413 algorithm selection */
	uint8_t mode;
	/* Size of the exported intermediate state */
	uint8_t state_sz;
	/* Size of the message length field in the padding (FIPS 180-4 5.1) */
	uint8_t length_field_sz;
	/* Digest size */
	uint8_t digest_sz;
};

enum {
	ALG_SHA224,
	ALG_SHA256,
	ALG_SHA384,
	ALG_SHA512,
};

static const struct sha2_alg algs[] = {
	[ALG_SHA224] = {BA413_CONFIG_SHA224, 32, 8, PSA_HASH_LENGTH(PSA_ALG_SHA_224)},
	[ALG_SHA256] = {BA413_CONFIG_SHA256, 32, 8, PSA_HASH_LENGTH(PSA_ALG_SHA_256)},
	[ALG_SHA384] = {BA413_CONFIG_SHA384, 64, 16, PSA_HASH_LENGTH(PSA_ALG_SHA_384)},
	[ALG_SHA512] = {BA413_CONFIG_SHA512, 64, 16, PSA_HASH_LENGTH(PSA_ALG_SHA_512)},
};

static const uint8_t block_sz[] = {
	[ALG_SHA224] = 64,
	[ALG_SHA256] = 64,
	[ALG_SHA384] = 128,
	[ALG_SHA512] = 128,
};

/* The hardware requires message data, an empty message is 4 ignored bytes */
static uint8_t empty_message[4];

static int sha2_alg_index(psa_algorithm_t alg)
{
	switch (alg) {
	case PSA_ALG_SHA_224:
		return IS_ENABLED(CONFIG_TF_PSA_CRYPTO_CRACEN_SHA224) ? ALG_SHA224 : -1;
	case PSA_ALG_SHA_256:
		return IS_ENABLED(CONFIG_TF_PSA_CRYPTO_CRACEN_SHA256) ? ALG_SHA256 : -1;
	case PSA_ALG_SHA_384:
		return IS_ENABLED(CONFIG_TF_PSA_CRYPTO_CRACEN_SHA384) ? ALG_SHA384 : -1;
	case PSA_ALG_SHA_512:
		return IS_ENABLED(CONFIG_TF_PSA_CRYPTO_CRACEN_SHA512) ? ALG_SHA512 : -1;
	default:
		return -1;
	}
}

/**
 * Run a BA413 job
 *
 * @param alg Algorithm parameters
 * @param cfg Configuration flags (in addition to the algorithm selection)
 * @param state_in Intermediate state to resume from, or NULL
 * @param d1 First message buffer
 * @param l1 Length of @a d1
 * @param d2 Second message buffer
 * @param l2 Length of @a d2
 * @param out State or digest output
 * @param out_len Length of @a out
 */
static int sha2_job(const struct sha2_alg *alg, uint32_t cfg, const uint32_t *state_in,
		    const uint8_t *d1, size_t l1, const uint8_t *d2, size_t l2, void *out,
		    size_t out_len)
{
	/* DMA source must be in RAM */
	uint32_t cfg_word = alg->mode | cfg;
	nrf_cracen_cm_dma_desc_t in[4];
	nrf_cracen_cm_dma_desc_t output;
	size_t ignored = (4 - ((l1 + l2) % 4)) % 4;
	int n_in = 0;

	in[n_in++] =
		cracen_cm_desc(&cfg_word, sizeof(cfg_word) | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
			       BA413_TAG_CONFIG);
	if (state_in != NULL) {
		in[n_in++] = cracen_cm_desc(state_in,
					    alg->state_sz | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
					    BA413_TAG_STATE);
	}
	if ((l1 + l2) == 0) {
		in[n_in++] = cracen_cm_desc(
			empty_message, sizeof(empty_message),
			BA413_TAG_MESSAGE | CM_DMA_TAG_INVALID_BYTES(sizeof(empty_message)));
	} else {
		/* Message descriptors are not realigned, so the data is contiguous */
		if (l1) {
			in[n_in++] = cracen_cm_desc(d1, l1, BA413_TAG_MESSAGE);
		}
		if (l2) {
			in[n_in++] = cracen_cm_desc(d2, l2, BA413_TAG_MESSAGE);
		}
		/* Final word of the message must be completed with ignored bytes */
		in[n_in - 1].length += ignored;
		in[n_in - 1].dmatag |= CM_DMA_TAG_INVALID_BYTES(ignored);
	}
	in[n_in - 1].length |= NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN;
	in[n_in - 1].dmatag |= NRF_CRACEN_CM_DMA_TAG_LAST;

	output = cracen_cm_desc(out, out_len | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
				NRF_CRACEN_CM_DMA_TAG_LAST);

	cracen_cm_chain(in, n_in);
	cracen_cm_chain(&output, 1);
	return cracen_cm_run(in, &output, BA413_ENGINE_MASK);
}

static int sha2_oneshot(int idx, const uint8_t *input, size_t len, uint8_t *digest)
{
	return sha2_job(&algs[idx], BA413_CONFIG_HW_PAD | BA413_CONFIG_FINAL, NULL, input, len,
			NULL, 0, digest, algs[idx].digest_sz);
}

static int sha2_blocks(struct cracen_hash_ctx *ctx, const uint8_t *input, size_t len)
{
	const struct sha2_alg *alg = &algs[ctx->alg];

	return sha2_job(alg, 0, ctx->resume ? ctx->state : NULL, ctx->buf, block_sz[ctx->alg],
			input, len, ctx->state, alg->state_sz);
}

static int sha2_finish(struct cracen_hash_ctx *ctx, uint8_t *digest)
{
	const struct sha2_alg *alg = &algs[ctx->alg];
	size_t bs = block_sz[ctx->alg];
	uint8_t padding[CRACEN_HASH_MAX_BLOCK_SZ + 1 + 16];
	size_t pad_len;

	if (!ctx->resume) {
		/* Complete message is buffered, hardware padding */
		return sha2_job(alg, BA413_CONFIG_HW_PAD | BA413_CONFIG_FINAL, NULL, ctx->buf,
				ctx->buf_len, NULL, 0, digest, alg->digest_sz);
	}

	/* Padding: 0x80, zeroes, message length in bits (FIPS 180-4 5.1) */
	pad_len = bs - (ctx->total % bs);
	if (pad_len < (1U + alg->length_field_sz)) {
		pad_len += bs;
	}
	memset(padding, 0, pad_len);
	padding[0] = 0x80;
	/* Upper 64 bits of a 128 bit length field are always zero */
	sys_put_be64(ctx->total * 8, padding + pad_len - 8);

	return sha2_job(alg, BA413_CONFIG_FINAL, ctx->state, ctx->buf, ctx->buf_len, padding,
			pad_len, digest, alg->digest_sz);
}

const struct cracen_hash_driver cracen_sha2_driver = {
	.engine_mask = BA413_ENGINE_MASK,
	.block_sz = block_sz,
	.alg_index = sha2_alg_index,
	.oneshot = sha2_oneshot,
	.blocks = sha2_blocks,
	.finish = sha2_finish,
};
