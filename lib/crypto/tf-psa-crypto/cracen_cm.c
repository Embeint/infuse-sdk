/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * Common CRACEN CryptoMaster DMA job handling.
 *
 * Access to the CRACEN is arbitrated by the Zephyr "nordic,nrf-cracen" device, which is shared
 * with the CRACEN CTR_DRBG entropy driver. The synchronisation mechanism (interrupt lock or
 * semaphore) is selected by CONFIG_NORDIC_CRACEN_SYNC.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/misc/nordic_cracen/nordic_cracen.h>

#include <hal/nrf_cracen.h>
#include <hal/nrf_cracen_cm.h>

#include "cracen_cm.h"

#define CRACEN_DEV DEVICE_DT_GET_ONE(nordic_nrf_cracen)

#define CM_STATUS_BUSY_MASK                                                                        \
	(NRF_CRACEN_CM_STATUS_BUSY_FETCH_MASK | NRF_CRACEN_CM_STATUS_BUSY_PUSH_MASK |              \
	 NRF_CRACEN_CM_STATUS_PUSH_WAITING_MASK)
#define CM_INT_BUS_ERROR_MASK                                                                      \
	(CRACENCORE_CRYPTMSTRDMA_INTSTATRAW_FETCHERERROR_Msk |                                     \
	 CRACENCORE_CRYPTMSTRDMA_INTSTATRAW_PUSHERERROR_Msk)

/* RAM accessible to the CryptoMaster DMA (excludes secure RAM in non-secure builds) */
#define CM_RAM_START DT_REG_ADDR(DT_CHOSEN(zephyr_sram))
#define CM_RAM_SIZE  DT_REG_SIZE(DT_CHOSEN(zephyr_sram))

/*
 * Upper bound on the duration of a job, only reached if the CryptoMaster stops making progress.
 * The largest jobs (16 MB descriptors) complete in well under this time.
 */
#define CM_JOB_TIMEOUT_MS 1000

/*
 * Busy polls between reads of the system clock while waiting for a job. Reading the clock (the
 * GRTC on nRF54L) takes far longer than a poll, and reading it on every poll delays detecting
 * the completion of each job by up to one read. Short jobs (e.g. single block hashes) complete
 * before the clock is ever read. The value only affects how soon after CM_JOB_TIMEOUT_MS a
 * stalled job is detected, by at most this many polls (a few microseconds), not correctness.
 * A power of two, so that the check is a mask.
 */
#define CM_POLLS_PER_CLOCK_READ 64

/* Engines present in the CryptoMaster, read on first use */
static uint32_t cm_engines;
static bool cm_engines_known;

bool cracen_cm_dma_accessible(const void *ptr, size_t len)
{
	uintptr_t start = (uintptr_t)ptr;

	if (len == 0) {
		return true;
	}
	/* Every byte the DMA reads or writes must be in RAM, a bus error is not recoverable */
	return (start >= CM_RAM_START) && (len <= CM_RAM_SIZE) &&
	       ((start - CM_RAM_START) <= (CM_RAM_SIZE - len));
}

/* Read the engines present in the CryptoMaster, must be called with the CRACEN held and the
 * CryptoMaster module enabled.
 */
static void cm_engines_read(void)
{
	if (!cm_engines_known) {
		cm_engines = NRF_CRACENCORE->CRYPTMSTRHW.INCLIPSHWCFG;
		cm_engines_known = true;
	}
}

bool cracen_cm_engine_available(uint32_t engine_mask)
{
	if (!cm_engines_known) {
		/* Probe on first use, so that missing engines are reported before an operation
		 * starts. Acquisition only fails in interrupt context while the CRACEN is in use,
		 * in which case the engine is assumed present and cracen_cm_run checks again.
		 */
		if (nordic_cracen_acquire(CRACEN_DEV, K_FOREVER) != 0) {
			return true;
		}
		nrf_cracen_module_enable(NRF_CRACEN, NRF_CRACEN_MODULE_CRYPTOMASTER_MASK);
		cm_engines_read();
		nrf_cracen_module_disable(NRF_CRACEN, NRF_CRACEN_MODULE_CRYPTOMASTER_MASK);
		nordic_cracen_release(CRACEN_DEV);
	}
	return (cm_engines & engine_mask) != 0;
}

void cracen_cm_chain(nrf_cracen_cm_dma_desc_t *descs, int num)
{
	for (int i = 0; i < num; i++) {
		descs[i].p_next = (i == num - 1) ? NRF_CRACEN_CM_DMA_DESC_STOP : &descs[i + 1];
	}
}

/*
 * Completion is detected by spinning on the busy flag (measured for ChaCha20-Poly1305, the same
 * reasoning applies to the other engines). With
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
int cracen_cm_run(nrf_cracen_cm_dma_desc_t *in, nrf_cracen_cm_dma_desc_t *out, uint32_t engine_mask)
{
	uint32_t timeout = k_ms_to_cyc_ceil32(CM_JOB_TIMEOUT_MS);
	uint32_t start = 0, now, busy;
	int rc = 0;

	if (nordic_cracen_acquire(CRACEN_DEV, K_FOREVER) != 0) {
		return -ENOTSUP;
	}
	/* Module power is managed by the CRACEN users, while holding the lock */
	nrf_cracen_module_enable(NRF_CRACEN, NRF_CRACEN_MODULE_CRYPTOMASTER_MASK);

	cm_engines_read();
	if (!(cm_engines & engine_mask)) {
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

	for (uint32_t polls = 1;; polls++) {
		/* Busy is read first, so an error raised before the job stopped is always seen */
		busy = nrf_cracen_cm_status_get(NRF_CRACENCORE, CM_STATUS_BUSY_MASK);
		if (nrf_cracen_cm_int_pending_get(NRF_CRACENCORE) & CM_INT_BUS_ERROR_MASK) {
			rc = -EIO;
			break;
		}
		if (!busy) {
			break;
		}
		if ((polls % CM_POLLS_PER_CLOCK_READ) == 0) {
			/* Timeout is measured from the first clock read */
			now = k_cycle_get_32();
			if (polls == CM_POLLS_PER_CLOCK_READ) {
				start = now;
			} else if ((now - start) > timeout) {
				rc = -ETIMEDOUT;
				break;
			}
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
