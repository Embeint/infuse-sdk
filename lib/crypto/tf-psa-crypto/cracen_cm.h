/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * Common CRACEN CryptoMaster DMA job handling.
 */

#ifndef INFUSE_LIB_CRYPTO_TF_PSA_CRYPTO_CRACEN_CM_H_
#define INFUSE_LIB_CRYPTO_TF_PSA_CRYPTO_CRACEN_CM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#include <hal/nrf_cracen_cm.h>
#include <helpers/nrf_cracen_cm_dma.h>

/*
 * Generic CryptoMaster DMA values not provided by nrfx, derived from the nRF Connect SDK
 * CRACEN driver (sxsymcrypt cmdma.h).
 */
/** Output descriptor flag to discard the data instead of writing it */
#define CM_DMA_DESC_LENGTH_DISCARD  BIT(30)
/** Number of invalid bytes in the last word of an input descriptor */
#define CM_DMA_TAG_INVALID_BYTES(n) ((n) << 8)
/** Descriptor length field size, padded lengths must fit */
#define CM_DMA_MAX_LEN              (0xFFFFFF & ~15U)

/**
 * @brief Check whether a buffer can be accessed by the CryptoMaster DMA
 *
 * @param ptr Buffer
 * @param len Buffer length
 *
 * @a len must include any bytes read past the end of the data (descriptor padding).
 *
 * @retval true Buffer is in RAM
 * @retval false Buffer cannot be accessed by the DMA
 */
bool cracen_cm_dma_accessible(const void *ptr, size_t len);

/**
 * @brief Check whether a CryptoMaster engine is present
 *
 * The engines present are read from the hardware on the first call, which acquires the CRACEN.
 *
 * @param engine_mask CRACENCORE_CRYPTMSTRHW_INCLIPSHWCFG_*_Msk of the engine
 *
 * @retval false Engine is not present
 * @retval true Engine is present, or presence could not yet be determined (CRACEN in use while
 *         called from interrupt context)
 */
bool cracen_cm_engine_available(uint32_t engine_mask);

/**
 * @brief Construct an unchained DMA descriptor
 *
 * The DMA never writes through input descriptors, so @a addr may be a const buffer.
 */
static inline nrf_cracen_cm_dma_desc_t cracen_cm_desc(const void *addr, uint32_t length,
						      uint32_t dmatag)
{
	return (nrf_cracen_cm_dma_desc_t){
		.p_addr = (uint8_t *)(uintptr_t)addr,
		.p_next = NULL,
		.length = length,
		.dmatag = dmatag,
	};
}

/**
 * @brief Chain an array of descriptors and terminate the chain
 *
 * @param descs Descriptor array
 * @param num Number of descriptors
 */
void cracen_cm_chain(nrf_cracen_cm_dma_desc_t *descs, int num);

/**
 * @brief Run a DMA job on the CryptoMaster
 *
 * Acquires the CRACEN, powers on the CryptoMaster, runs the job until completion and releases
 * the CRACEN again.
 *
 * @param in Chained input descriptors
 * @param out Chained output descriptors
 * @param engine_mask CRACENCORE_CRYPTMSTRHW_INCLIPSHWCFG_*_Msk of the engine used by the job
 *
 * @retval 0 on success
 * @retval -ENOTSUP CRACEN could not be acquired, or the engine is not present. No buffers
 *         have been modified.
 * @retval -EIO DMA bus error, output buffers may have been modified
 * @retval -ETIMEDOUT Job did not complete, output buffers may have been modified
 */
int cracen_cm_run(nrf_cracen_cm_dma_desc_t *in, nrf_cracen_cm_dma_desc_t *out,
		  uint32_t engine_mask);

#endif /* INFUSE_LIB_CRYPTO_TF_PSA_CRYPTO_CRACEN_CM_H_ */
