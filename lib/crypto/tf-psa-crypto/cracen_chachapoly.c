/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * One-shot ChaCha20-Poly1305 on the CRACEN CryptoMaster (BA417 engine).
 *
 * By default the complete AEAD operation is a single scatter-gather DMA job:
 *   in:  config, key, counter (1), nonce, AAD (padded), data (padded), lengths
 *   out: discarded AAD, data, tag
 *
 * On devices where the combined ChaCha20-Poly1305 mode cannot be used
 * (nRF54LM20), the BA417 is run in plain ChaCha20 mode instead and Poly1305 is
 * computed in software. This is still a single DMA job:
 *   in:  config, key, counter (0), nonce, 64 zero bytes, data (padded)
 *   out: Poly1305 key block, data
 *
 * Jobs are run by the common CryptoMaster handling (cracen_cm.c). If the CRACEN cannot be
 * acquired, the caller falls back to software (-ENOTSUP).
 */

#define __STDC_WANT_LIB_EXT1__ 1 /* Ask for the C11 memset_s() */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>

#include "ocrypto_poly1305.h"

#include "cracen_chachapoly.h"
#include "cracen_cm.h"

/* Picolibc supports memset_s but doesn't define __STDC_LIB_EXT1__ */
#if !defined(__STDC_LIB_EXT1__) && !defined(CONFIG_PICOLIBC)
#error "memset_s() is required to clear sensitive data"
#endif

/*
 * BA417 (ChaCha20-Poly1305) engine specific values. These are not documented by nrfx or the
 * MDK, and were derived from the nRF Connect SDK CRACEN driver (sxsymcrypt cmdma.h, cmaes.h
 * and chachapoly.c).
 */
#define BA417_TAG_ENGINE           0x4 /* DMA tag engine selection */
#define BA417_REG_OFFSET_CONFIG    0x00
#define BA417_REG_OFFSET_KEY       0x04
#define BA417_REG_OFFSET_COUNTER   0x28
#define BA417_REG_OFFSET_NONCE     0x2C
#define BA417_CONFIG_MODE_AEAD     0x0 /* ChaCha20-Poly1305 */
#define BA417_CONFIG_MODE_CHACHA20 0x1
#define BA417_CONFIG_DECRYPT       BIT(2)

/* Tag for a BA417 configuration register write */
#define BA417_TAG_CONFIG(reg_off)                                                                  \
	(BA417_TAG_ENGINE | NRF_CRACEN_CM_DMA_TAG_CONFIG | ((reg_off) << 8))
/* Data that is authenticated but not encrypted (AAD) */
#define BA417_TAG_DATATYPE_HEADER (1 << NRF_CRACEN_CM_DMA_TAG_DATATYPE_OFFSET)

#define BA417_ENGINE_MASK CRACENCORE_CRYPTMSTRHW_INCLIPSHWCFG_BA417CHACHAPOLYINCLUDED_Msk

#define CHACHAPOLY_KEY_SZ   32
#define CHACHAPOLY_NONCE_SZ 12
#define CHACHAPOLY_TAG_SZ   16
#define CHACHA20_BLOCK_SZ   64

#define PAD16(sz) (((sz) + 15) & ~15U)

/* nRF54LM20 CRACEN only supports the BA417 in plain ChaCha20 mode */
#define SW_POLY1305 IS_ENABLED(CONFIG_TF_PSA_CRYPTO_CRACEN_CHACHA20_POLY1305_SW_POLY1305)

/* Configuration words and initial block counters (DMA sources must be in RAM) */
static uint32_t cfg_words[3] = {
	BA417_CONFIG_MODE_AEAD,
	BA417_CONFIG_MODE_AEAD | BA417_CONFIG_DECRYPT,
	BA417_CONFIG_MODE_CHACHA20,
};
static uint8_t counter_aead[4] = {0, 0, 0, 1};
static uint8_t counter_zero[4];
static uint8_t zero_block[CHACHA20_BLOCK_SZ];

static bool overlaps(const uint8_t *a, const uint8_t *b, size_t len)
{
	/* Pointers to separate objects cannot be compared directly. Both buffers are DMA
	 * accessible (in RAM), so the additions cannot overflow.
	 */
	uintptr_t ua = (uintptr_t)a, ub = (uintptr_t)b;

	return (len > 0) && (ua < ub + len) && (ub < ua + len);
}

bool cracen_chachapoly_supported(const uint8_t *key, const uint8_t *nonce, size_t nonce_len,
				 const uint8_t *aad, size_t aad_len, const uint8_t *input,
				 const uint8_t *output, size_t len)
{
	/* Input descriptors read up to the next multiple of 16 bytes, outputs are exact. The AAD is
	 * not read by the DMA when Poly1305 is computed in software.
	 */
	return cracen_cm_engine_available(BA417_ENGINE_MASK) &&
	       (nonce_len == CHACHAPOLY_NONCE_SZ) && (len <= CM_DMA_MAX_LEN) &&
	       (aad_len <= CM_DMA_MAX_LEN) && cracen_cm_dma_accessible(key, CHACHAPOLY_KEY_SZ) &&
	       cracen_cm_dma_accessible(nonce, nonce_len) &&
	       (SW_POLY1305 || cracen_cm_dma_accessible(aad, PAD16(aad_len))) &&
	       cracen_cm_dma_accessible(input, PAD16(len)) && cracen_cm_dma_accessible(output, len);
}

static void length_block(uint8_t lengths[16], size_t aad_len, size_t len)
{
	uint64_t aad_bytes = aad_len, data_bytes = len;

	for (int i = 0; i < 8; i++) {
		lengths[i] = (uint8_t)(aad_bytes >> (8 * i));
		lengths[8 + i] = (uint8_t)(data_bytes >> (8 * i));
	}
}

/* Complete AEAD operation on the BA417 */
static int crypt_aead_mode(bool decrypt, const uint8_t key[32], const uint8_t nonce[12],
			   const uint8_t *aad, size_t aad_len, const uint8_t *input, size_t len,
			   uint8_t *output, uint8_t tag[16])
{
	nrf_cracen_cm_dma_desc_t in[7];
	nrf_cracen_cm_dma_desc_t out[5];
	uint8_t lengths[16];
	int n_in = 0, n_out = 0;

	length_block(lengths, aad_len, len);

	/* Input descriptors */
	in[n_in++] = cracen_cm_desc(&cfg_words[decrypt ? 1 : 0],
				    sizeof(uint32_t) | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
				    BA417_TAG_CONFIG(BA417_REG_OFFSET_CONFIG));
	in[n_in++] = cracen_cm_desc(key, CHACHAPOLY_KEY_SZ | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
				    BA417_TAG_CONFIG(BA417_REG_OFFSET_KEY));
	in[n_in++] = cracen_cm_desc(counter_aead,
				    sizeof(counter_aead) | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
				    BA417_TAG_CONFIG(BA417_REG_OFFSET_COUNTER));
	in[n_in++] =
		cracen_cm_desc(nonce, CHACHAPOLY_NONCE_SZ | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
			       BA417_TAG_CONFIG(BA417_REG_OFFSET_NONCE));
	if (aad_len) {
		in[n_in++] =
			cracen_cm_desc(aad, PAD16(aad_len) | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
				       BA417_TAG_ENGINE | BA417_TAG_DATATYPE_HEADER |
					       CM_DMA_TAG_INVALID_BYTES(PAD16(aad_len) - aad_len));
	}
	if (len) {
		in[n_in++] = cracen_cm_desc(
			input, PAD16(len) | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
			BA417_TAG_ENGINE | CM_DMA_TAG_INVALID_BYTES(PAD16(len) - len));
	}
	in[n_in++] =
		cracen_cm_desc(lengths, sizeof(lengths) | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
			       BA417_TAG_ENGINE | NRF_CRACEN_CM_DMA_TAG_LAST);

	/* Output descriptors */
	if (aad_len) {
		out[n_out++] = cracen_cm_desc(NULL, PAD16(aad_len) | CM_DMA_DESC_LENGTH_DISCARD, 0);
	}
	if (len) {
		out[n_out++] = cracen_cm_desc(output, len, 0);
		if (PAD16(len) != len) {
			out[n_out++] = cracen_cm_desc(
				NULL, (PAD16(len) - len) | CM_DMA_DESC_LENGTH_DISCARD, 0);
		}
	}
	out[n_out++] =
		cracen_cm_desc(tag, CHACHAPOLY_TAG_SZ | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
			       NRF_CRACEN_CM_DMA_TAG_LAST);

	cracen_cm_chain(in, n_in);
	cracen_cm_chain(out, n_out);
	return cracen_cm_run(in, out, BA417_ENGINE_MASK);
}

/* ChaCha20 on the BA417 (counter 0 output to poly_key), Poly1305 in software */
static int crypt_sw_poly1305(bool decrypt, const uint8_t key[32], const uint8_t nonce[12],
			     const uint8_t *aad, size_t aad_len, const uint8_t *input, size_t len,
			     uint8_t *output, uint8_t tag[16])
{
	ocrypto_poly1305_ctx poly_ctx;
	uint8_t poly_key[CHACHA20_BLOCK_SZ];
	nrf_cracen_cm_dma_desc_t in[6];
	nrf_cracen_cm_dma_desc_t out[3];
	uint8_t lengths[16];
	int n_in = 0, n_out = 0;
	int rc;

	/* The ciphertext is authenticated after the job, so decryption must not overwrite it */
	if (decrypt && overlaps(input, output, len)) {
		return -ENOTSUP;
	}

	/* Input descriptors */
	in[n_in++] = cracen_cm_desc(&cfg_words[2],
				    sizeof(uint32_t) | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
				    BA417_TAG_CONFIG(BA417_REG_OFFSET_CONFIG));
	in[n_in++] = cracen_cm_desc(key, CHACHAPOLY_KEY_SZ | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
				    BA417_TAG_CONFIG(BA417_REG_OFFSET_KEY));
	in[n_in++] = cracen_cm_desc(counter_zero,
				    sizeof(counter_zero) | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
				    BA417_TAG_CONFIG(BA417_REG_OFFSET_COUNTER));
	in[n_in++] =
		cracen_cm_desc(nonce, CHACHAPOLY_NONCE_SZ | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
			       BA417_TAG_CONFIG(BA417_REG_OFFSET_NONCE));
	in[n_in++] = cracen_cm_desc(zero_block,
				    sizeof(zero_block) | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
				    BA417_TAG_ENGINE);
	if (len) {
		in[n_in++] = cracen_cm_desc(
			input, PAD16(len) | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
			BA417_TAG_ENGINE | CM_DMA_TAG_INVALID_BYTES(PAD16(len) - len));
	}
	in[n_in - 1].dmatag |= NRF_CRACEN_CM_DMA_TAG_LAST;

	/* Output descriptors */
	out[n_out++] = cracen_cm_desc(poly_key, sizeof(poly_key), 0);
	if (len) {
		out[n_out++] = cracen_cm_desc(output, len, 0);
		if (PAD16(len) != len) {
			out[n_out++] = cracen_cm_desc(
				NULL, (PAD16(len) - len) | CM_DMA_DESC_LENGTH_DISCARD, 0);
		}
	}
	out[n_out - 1].length |= NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN;
	out[n_out - 1].dmatag |= NRF_CRACEN_CM_DMA_TAG_LAST;

	cracen_cm_chain(in, n_in);
	cracen_cm_chain(out, n_out);
	rc = cracen_cm_run(in, out, BA417_ENGINE_MASK);
	if (rc == 0) {
		/* Poly1305 over AAD and ciphertext (RFC 8439 section 2.8) */
		length_block(lengths, aad_len, len);
		ocrypto_poly1305_init(&poly_ctx, poly_key);
		ocrypto_poly1305_update(&poly_ctx, aad, aad_len);
		ocrypto_poly1305_pad(&poly_ctx);
		ocrypto_poly1305_update(&poly_ctx, decrypt ? input : output, len);
		ocrypto_poly1305_pad(&poly_ctx);
		ocrypto_poly1305_update(&poly_ctx, lengths, sizeof(lengths));
		ocrypto_poly1305_final(&poly_ctx, tag);
	}
	memset_s(poly_key, sizeof(poly_key), 0, sizeof(poly_key));
	memset_s(&poly_ctx, sizeof(poly_ctx), 0, sizeof(poly_ctx));
	return rc;
}

int cracen_chachapoly_crypt(bool decrypt, const uint8_t key[32], const uint8_t nonce[12],
			    const uint8_t *aad, size_t aad_len, const uint8_t *input, size_t len,
			    uint8_t *output, uint8_t tag[16])
{
	if (SW_POLY1305) {
		return crypt_sw_poly1305(decrypt, key, nonce, aad, aad_len, input, len, output,
					 tag);
	}
	return crypt_aead_mode(decrypt, key, nonce, aad, aad_len, input, len, output, tag);
}
