/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * https://docs.nordicsemi.com/bundle/ps_nrf54L15/page/debug.html
 */

#include <zephyr/sys/reboot.h>

#include <nrfx_rramc.h>
#include <nrf.h>

void infuse_security_disable_dap(void)
{
	if ((NRF_UICR_S->APPROTECT[0].PROTECT0 & UICR_APPROTECT_PROTECT0_PALL_Msk) !=
	    UICR_APPROTECT_PROTECT0_PALL_Unprotected) {
		/* APPROTECT already written to enabled state */
		return;
	}

	/* Unbuffered writes */
	nrf_rramc_config_t config = {.mode_write = true, .write_buff_size = 0};

	/* Enable write mode */
	nrf_rramc_config_set(NRF_RRAMC, &config);

	/* Write PROTECT registers to a non-default value */
	NRF_UICR_S->SECUREAPPROTECT[0].PROTECT0 = 0xAA55AA55;
	while (!nrf_rramc_ready_check(NRF_RRAMC)) {
	}
	NRF_UICR_S->APPROTECT[0].PROTECT0 = 0xAA55AA55;
	while (!nrf_rramc_ready_check(NRF_RRAMC)) {
	}

	/* Disable write mode */
	config.mode_write = false;
	nrf_rramc_config_set(NRF_RRAMC, &config);

	/* Reboot so that configuration is applied */
	sys_reboot(SYS_REBOOT_WARM);
}
