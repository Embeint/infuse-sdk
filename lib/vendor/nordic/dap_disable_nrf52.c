/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * From Nordic blog post:
 * https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/working-with-the-nrf52-series-improved-approtect
 */

#include <zephyr/sys/reboot.h>

#include <nrf.h>

void infuse_security_disable_dap(void)
{
	if ((NRF_UICR->APPROTECT & UICR_APPROTECT_PALL_Msk) ==
	    (UICR_APPROTECT_PALL_Enabled << UICR_APPROTECT_PALL_Pos)) {
		/* APPROTECT already written to enabled state */
		return;
	}
	/* Enable writing to flash */
	NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
	while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {
	}
	/* Write the APPROTECT register to enabled state */
	NRF_UICR->APPROTECT = ((NRF_UICR->APPROTECT & ~((uint32_t)UICR_APPROTECT_PALL_Msk)) |
			       (UICR_APPROTECT_PALL_Enabled << UICR_APPROTECT_PALL_Pos));

	/* Wait for the write to complete */
	NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
	while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {
	}

	/* Reboot so that configuration is applied */
	sys_reboot(SYS_REBOOT_WARM);
}
