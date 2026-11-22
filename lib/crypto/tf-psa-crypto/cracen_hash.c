/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * Common handling of hash algorithms on the CRACEN CryptoMaster.
 *
 * Multi-part operations buffer the input into blocks. Each update runs at most one job,
 * processing the buffered block together with all complete blocks of the input that the DMA
 * can access directly. At least one byte is always kept buffered, so that the final job has
 * data to pad.
 *
 * There is no software fallback (the builtin implementations are removed through
 * MBEDTLS_PSA_ACCEL_ALG_*). Acquiring the CRACEN always succeeds with
 * NORDIC_CRACEN_SYNC_IRQ_LOCK, and before the kernel has started. With
 * NORDIC_CRACEN_SYNC_SEMAPHORE it only fails in interrupt context while a thread is using the
 * CRACEN, as hashing is not expected to be used from interrupts.
 */

#define __STDC_WANT_LIB_EXT1__ 1 /* Ask for the C11 memset_s() */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <psa/crypto.h>

#include "cracen_cm.h"
#include "cracen_hash.h"

/* Picolibc supports memset_s but doesn't define __STDC_LIB_EXT1__ */
#if !defined(__STDC_LIB_EXT1__) && !defined(CONFIG_PICOLIBC)
#error "memset_s() is required to clear sensitive data"
#endif

static const struct cracen_hash_driver *const drivers[] = {
#ifdef CONFIG_TF_PSA_CRYPTO_CRACEN_SHA2
	&cracen_sha2_driver,
#endif
#ifdef CONFIG_TF_PSA_CRYPTO_CRACEN_SHA3
	&cracen_sha3_driver,
#endif
};

static int lookup(psa_algorithm_t alg, const struct cracen_hash_driver **drv)
{
	int idx;

	for (int i = 0; i < ARRAY_SIZE(drivers); i++) {
		idx = drivers[i]->alg_index(alg);
		if (idx >= 0) {
			*drv = drivers[i];
			return idx;
		}
	}
	return -1;
}

/* Largest multiple of the block size that fits in a DMA descriptor */
static size_t max_job_len(size_t block_sz)
{
	return CM_DMA_MAX_LEN - (CM_DMA_MAX_LEN % block_sz);
}

bool cracen_hash_supported(psa_algorithm_t alg)
{
	const struct cracen_hash_driver *drv;

	return lookup(alg, &drv) >= 0;
}

int cracen_hash_setup(struct cracen_hash_ctx *ctx, psa_algorithm_t alg)
{
	const struct cracen_hash_driver *drv;
	int idx = lookup(alg, &drv);

	if ((idx < 0) || !cracen_cm_engine_available(drv->engine_mask)) {
		return -ENOTSUP;
	}
	memset(ctx, 0, sizeof(*ctx));
	ctx->drv = drv;
	ctx->alg = idx;
	return 0;
}

int cracen_hash_update(struct cracen_hash_ctx *ctx, const uint8_t *input, size_t len)
{
	size_t block_sz = ctx->drv->block_sz[ctx->alg];
	size_t take, direct;
	int rc;

	while (len > 0) {
		if (ctx->buf_len == block_sz) {
			/* Process the buffered block and the complete input blocks the DMA can
			 * access, always keeping at least one byte of the input for later.
			 */
			direct = 0;
			if (cracen_cm_dma_accessible(input, len)) {
				direct = MIN(((len - 1) / block_sz) * block_sz,
					     max_job_len(block_sz));
			}
			rc = ctx->drv->blocks(ctx, input, direct);
			if (rc != 0) {
				return rc;
			}
			ctx->resume = true;
			ctx->buf_len = 0;
			ctx->total += direct;
			input += direct;
			len -= direct;
		}
		take = MIN(block_sz - ctx->buf_len, len);
		memcpy(ctx->buf + ctx->buf_len, input, take);
		ctx->buf_len += take;
		ctx->total += take;
		input += take;
		len -= take;
	}
	return 0;
}

int cracen_hash_finish(struct cracen_hash_ctx *ctx, uint8_t *digest)
{
	int rc = ctx->drv->finish(ctx, digest);

	memset_s(ctx, sizeof(*ctx), 0, sizeof(*ctx));
	return rc;
}

int cracen_hash_compute(psa_algorithm_t alg, const uint8_t *input, size_t len, uint8_t *digest)
{
	const struct cracen_hash_driver *drv;
	struct cracen_hash_ctx ctx;
	int idx = lookup(alg, &drv);
	int rc;

	if ((idx < 0) || !cracen_cm_engine_available(drv->engine_mask)) {
		return -ENOTSUP;
	}
	/* The final descriptor may be extended to a multiple of 4 bytes */
	if (cracen_cm_dma_accessible(input, ROUND_UP(len, 4)) &&
	    (len <= max_job_len(drv->block_sz[idx]))) {
		/* Single job directly from the input */
		return drv->oneshot(idx, input, len, digest);
	}

	/* Input not accessible by the DMA, or too large for a single descriptor */
	rc = cracen_hash_setup(&ctx, alg);
	if (rc == 0) {
		rc = cracen_hash_update(&ctx, input, len);
	}
	if (rc == 0) {
		rc = cracen_hash_finish(&ctx, digest);
	}
	memset_s(&ctx, sizeof(ctx), 0, sizeof(ctx));
	return rc;
}
