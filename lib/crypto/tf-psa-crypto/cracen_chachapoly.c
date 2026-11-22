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
 * Access to the CRACEN is arbitrated by the Zephyr "nordic,nrf-cracen" device,
 * which is shared with the CRACEN CTR_DRBG entropy driver. The synchronisation
 * mechanism (interrupt lock or semaphore) is selected by
 * CONFIG_NORDIC_CRACEN_SYNC. Completion is detected by polling the CryptoMaster
 * busy flag (see cm_run). If the CRACEN cannot be acquired (in use while called
 * from interrupt context with the semaphore), the caller falls back to
 * software (-ENOTSUP).
 */

#define __STDC_WANT_LIB_EXT1__ 1 /* Ask for the C11 memset_s() */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/misc/nordic_cracen/nordic_cracen.h>

#include <hal/nrf_cracen.h>
#include <hal/nrf_cracen_cm.h>
#include <helpers/nrf_cracen_cm_dma.h>

#include "ocrypto_poly1305.h"

#include "cracen_chachapoly.h"

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

/*
 * Generic CryptoMaster DMA values not provided by nrfx, derived from the nRF Connect SDK
 * CRACEN driver (sxsymcrypt cmdma.h).
 */
/* Output descriptor flag to discard the data instead of writing it */
#define CM_DMA_DESC_LENGTH_DISCARD  BIT(30)
/* Number of invalid bytes in the last word of an input descriptor */
#define CM_DMA_TAG_INVALID_BYTES(n) ((n) << 8)
/* Descriptor length field size, padded lengths must fit */
#define CM_DMA_MAX_LEN              (0xFFFFFF & ~15U)

#define CM_STATUS_BUSY_MASK                                                                        \
	(NRF_CRACEN_CM_STATUS_BUSY_FETCH_MASK | NRF_CRACEN_CM_STATUS_BUSY_PUSH_MASK |              \
	 NRF_CRACEN_CM_STATUS_PUSH_WAITING_MASK)
#define CM_INT_BUS_ERROR_MASK                                                                      \
	(CRACENCORE_CRYPTMSTRDMA_INTSTATRAW_FETCHERERROR_Msk |                                     \
	 CRACENCORE_CRYPTMSTRDMA_INTSTATRAW_PUSHERERROR_Msk)

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

/* BA417 engine presence: 0 = unknown, 1 = present, -1 = not present */
static int ba417_present;

#define CRACEN_DEV DEVICE_DT_GET_ONE(nordic_nrf_cracen)

/* RAM accessible to the CryptoMaster DMA (excludes secure RAM in non-secure builds) */
#define CM_RAM_START DT_REG_ADDR(DT_CHOSEN(zephyr_sram))
#define CM_RAM_SIZE  DT_REG_SIZE(DT_CHOSEN(zephyr_sram))

/*
 * Upper bound on the duration of a job, only reached if the CryptoMaster stops making progress.
 * The largest jobs (16 MB descriptors) complete in well under this time.
 */
#define CM_JOB_TIMEOUT_MS 1000

/* Every byte the DMA reads or writes must be in RAM, a bus error is not recoverable */
static bool in_ram(const void *ptr, size_t len)
{
	uintptr_t start = (uintptr_t)ptr;

	if (len == 0) {
		return true;
	}
	return (start >= CM_RAM_START) && (len <= CM_RAM_SIZE) &&
	       ((start - CM_RAM_START) <= (CM_RAM_SIZE - len));
}

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
	return (ba417_present >= 0) && (nonce_len == CHACHAPOLY_NONCE_SZ) &&
	       (len <= CM_DMA_MAX_LEN) && (aad_len <= CM_DMA_MAX_LEN) &&
	       in_ram(key, CHACHAPOLY_KEY_SZ) && in_ram(nonce, nonce_len) &&
	       (SW_POLY1305 || in_ram(aad, PAD16(aad_len))) && in_ram(input, PAD16(len)) &&
	       in_ram(output, len);
}

/* Construct an unchained DMA descriptor (the DMA never writes through input descriptors) */
static nrf_cracen_cm_dma_desc_t desc(const void *addr, uint32_t length, uint32_t dmatag)
{
	return (nrf_cracen_cm_dma_desc_t){
		.p_addr = (uint8_t *)(uintptr_t)addr,
		.p_next = NULL,
		.length = length,
		.dmatag = dmatag,
	};
}

static void chain(nrf_cracen_cm_dma_desc_t *descs, int num)
{
	for (int i = 0; i < num; i++) {
		descs[i].p_next = (i == num - 1) ? NRF_CRACEN_CM_DMA_DESC_STOP : &descs[i + 1];
	}
}

/* Run a DMA job on the CryptoMaster */
static int cm_run(nrf_cracen_cm_dma_desc_t *in, nrf_cracen_cm_dma_desc_t *out)
{
	uint32_t timeout = k_ms_to_cyc_ceil32(CM_JOB_TIMEOUT_MS);
	uint32_t start, busy;
	int rc = 0;

	if (nordic_cracen_acquire(CRACEN_DEV, K_FOREVER) != 0) {
		return -ENOTSUP;
	}
	/* Module power is managed by the CRACEN users, while holding the lock */
	nrf_cracen_module_enable(NRF_CRACEN, NRF_CRACEN_MODULE_CRYPTOMASTER_MASK);

	if (ba417_present == 0) {
		uint32_t presence = NRF_CRACENCORE->CRYPTMSTRHW.INCLIPSHWCFG;

		ba417_present =
			(presence & CRACENCORE_CRYPTMSTRHW_INCLIPSHWCFG_BA417CHACHAPOLYINCLUDED_Msk)
				? 1
				: -1;
	}
	if (ba417_present < 0) {
		rc = -ENOTSUP;
		goto release;
	}

	nrf_cracen_cm_int_clear(NRF_CRACENCORE, ~0U);
	nrf_cracen_cm_fetch_addr_set(NRF_CRACENCORE, in);
	nrf_cracen_cm_push_addr_set(NRF_CRACENCORE, out);
	nrf_cracen_cm_config_indirect_set(
		NRF_CRACENCORE,
		(nrf_cracen_cm_config_indirect_mask_t)(NRF_CRACEN_CM_CONFIG_INDIRECT_FETCH_MASK |
						       NRF_CRACEN_CM_CONFIG_INDIRECT_PUSH_MASK));
	/* Descriptors must be visible before the DMA starts */
	__DMB();
	nrf_cracen_cm_start(NRF_CRACENCORE);
	start = k_cycle_get_32();

	/*
	 * Completion is detected by spinning on the busy flag. With
	 * CONFIG_NORDIC_CRACEN_SYNC_SEMAPHORE interrupts remain enabled while spinning, with
	 * CONFIG_NORDIC_CRACEN_SYNC_IRQ_LOCK they are locked for the duration of the job.
	 *
	 * Spinning vs. waiting on the CryptoMaster interrupt (measured on nRF54L15 @ 128 MHz):
	 *  - The job itself costs ~1.6 CPU cycles per byte (~1.6k cycles for 1 kB).
	 *  - Blocking on a semaphore given from the completion interrupt added a fixed ~2.7k
	 *    cycles (switch to idle, wake-up, ISR, switch back), and is slower for the caller at
	 *    every length. Other threads only gain CPU time once the job takes longer than the
	 *    switching overhead (~2 kB), and only a few microseconds for packet sized buffers.
	 *  - With the semaphore, ISRs and higher priority threads still preempt while
	 *    spinning. Only threads of equal or lower priority are delayed (~13 us per kB).
	 *    For large buffers (tens of kB) waiting on the interrupt would free most of that
	 *    time for other threads, but Oberon would delay them far longer.
	 *
	 * Spinning vs. the Oberon software implementation:
	 *  - The CPU is fully occupied for the duration of the operation either way, but the
	 *    CRACEN path finishes 2.6x (1 byte) to 12x (1 kB) sooner in the combined
	 *    ChaCha20-Poly1305 mode, and 1.7x to 3.7x sooner with Poly1305 in software, so
	 *    spinning is a net CPU saving at every length.
	 *  - The exception is contention: a thread that finds the CRACEN busy blocks in
	 *    nordic_cracen_acquire until the current job finishes (~13 us per kB), where
	 *    Oberon would have started immediately. Falling back to Oberon on contention
	 *    (K_NO_WAIT) would avoid the wait, at the cost of the slower software
	 *    implementation for that operation.
	 */
	while (true) {
		/* Busy is read first, so an error raised before the job stopped is always seen */
		busy = nrf_cracen_cm_status_get(NRF_CRACENCORE, CM_STATUS_BUSY_MASK);
		if (nrf_cracen_cm_int_pending_get(NRF_CRACENCORE) & CM_INT_BUS_ERROR_MASK) {
			rc = -EIO;
			break;
		}
		if (!busy) {
			break;
		}
		if ((k_cycle_get_32() - start) > timeout) {
			rc = -ETIMEDOUT;
			break;
		}
	}
	/* Stop the DMA after an error, and always leave the CryptoMaster in the reset state that
	 * the other CRACEN users (nrfx) expect.
	 */
	nrf_cracen_cm_softreset(NRF_CRACENCORE);
	nrf_cracen_cm_int_clear(NRF_CRACENCORE, ~0U);
release:
	nrf_cracen_module_disable(NRF_CRACEN, NRF_CRACEN_MODULE_CRYPTOMASTER_MASK);
	nordic_cracen_release(CRACEN_DEV);
	return rc;
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
	in[n_in++] = desc(&cfg_words[decrypt ? 1 : 0],
			  sizeof(uint32_t) | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
			  BA417_TAG_CONFIG(BA417_REG_OFFSET_CONFIG));
	in[n_in++] = desc(key, CHACHAPOLY_KEY_SZ | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
			  BA417_TAG_CONFIG(BA417_REG_OFFSET_KEY));
	in[n_in++] =
		desc(counter_aead, sizeof(counter_aead) | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
		     BA417_TAG_CONFIG(BA417_REG_OFFSET_COUNTER));
	in[n_in++] = desc(nonce, CHACHAPOLY_NONCE_SZ | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
			  BA417_TAG_CONFIG(BA417_REG_OFFSET_NONCE));
	if (aad_len) {
		in[n_in++] = desc(aad, PAD16(aad_len) | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
				  BA417_TAG_ENGINE | BA417_TAG_DATATYPE_HEADER |
					  CM_DMA_TAG_INVALID_BYTES(PAD16(aad_len) - aad_len));
	}
	if (len) {
		in[n_in++] = desc(input, PAD16(len) | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
				  BA417_TAG_ENGINE | CM_DMA_TAG_INVALID_BYTES(PAD16(len) - len));
	}
	in[n_in++] = desc(lengths, sizeof(lengths) | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
			  BA417_TAG_ENGINE | NRF_CRACEN_CM_DMA_TAG_LAST);

	/* Output descriptors */
	if (aad_len) {
		out[n_out++] = desc(NULL, PAD16(aad_len) | CM_DMA_DESC_LENGTH_DISCARD, 0);
	}
	if (len) {
		out[n_out++] = desc(output, len, 0);
		if (PAD16(len) != len) {
			out[n_out++] =
				desc(NULL, (PAD16(len) - len) | CM_DMA_DESC_LENGTH_DISCARD, 0);
		}
	}
	out[n_out++] = desc(tag, CHACHAPOLY_TAG_SZ | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
			    NRF_CRACEN_CM_DMA_TAG_LAST);

	chain(in, n_in);
	chain(out, n_out);
	return cm_run(in, out);
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
	in[n_in++] = desc(&cfg_words[2], sizeof(uint32_t) | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
			  BA417_TAG_CONFIG(BA417_REG_OFFSET_CONFIG));
	in[n_in++] = desc(key, CHACHAPOLY_KEY_SZ | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
			  BA417_TAG_CONFIG(BA417_REG_OFFSET_KEY));
	in[n_in++] =
		desc(counter_zero, sizeof(counter_zero) | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
		     BA417_TAG_CONFIG(BA417_REG_OFFSET_COUNTER));
	in[n_in++] = desc(nonce, CHACHAPOLY_NONCE_SZ | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
			  BA417_TAG_CONFIG(BA417_REG_OFFSET_NONCE));
	in[n_in++] = desc(zero_block, sizeof(zero_block) | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
			  BA417_TAG_ENGINE);
	if (len) {
		in[n_in++] = desc(input, PAD16(len) | NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN,
				  BA417_TAG_ENGINE | CM_DMA_TAG_INVALID_BYTES(PAD16(len) - len));
	}
	in[n_in - 1].dmatag |= NRF_CRACEN_CM_DMA_TAG_LAST;

	/* Output descriptors */
	out[n_out++] = desc(poly_key, sizeof(poly_key), 0);
	if (len) {
		out[n_out++] = desc(output, len, 0);
		if (PAD16(len) != len) {
			out[n_out++] =
				desc(NULL, (PAD16(len) - len) | CM_DMA_DESC_LENGTH_DISCARD, 0);
		}
	}
	out[n_out - 1].length |= NRF_CRACEN_CM_DMA_DESC_LENGTH_REALIGN;
	out[n_out - 1].dmatag |= NRF_CRACEN_CM_DMA_TAG_LAST;

	chain(in, n_in);
	chain(out, n_out);
	rc = cm_run(in, out);
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
