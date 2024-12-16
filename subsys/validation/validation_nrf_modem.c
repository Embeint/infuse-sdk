/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <infuse/validation/core.h>
#include <infuse/validation/nrf_modem.h>

#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>
#include <modem/modem_info.h>
#include <nrf_modem_at.h>

#define TEST "MODEM"

static int infuse_modem_info(void)
{
	char storage[64];
	uint64_t imei;

	/* Model identifier */
	if (nrf_modem_at_scanf("AT+CGMM", "%64s\n", storage) != 1) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to read model identifier");
		return -EIO;
	}
	VALIDATION_REPORT_INFO(TEST, "%16s: %s", "Modem Model", storage);

	/* Modem ESN */
	if (nrf_modem_at_scanf("AT+CGSN=0", "%64s\n", storage) != 1) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to read ESN");
		return -EIO;
	}
	VALIDATION_REPORT_INFO(TEST, "%16s: %s", "Modem ESN", storage);

	/* Modem IMEI */
	if (nrf_modem_at_scanf("AT+CGSN=1", "+CGSN: \"%" SCNd64 "\"\n", &imei) != 1) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to read IMEI");
		return -EIO;
	}
	VALIDATION_REPORT_INFO(TEST, "%16s: %s", "Modem IMEI", storage);
	/* Modem firmware revision */
	if (nrf_modem_at_scanf("AT+CGMR", "%64s\n", storage) != 1) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to read firmware version");
		return -EIO;
	}
	VALIDATION_REPORT_INFO(TEST, "%16s: %s", "Firmware Version", storage);

	return 0;
}

static int infuse_sim_card(void)
{
	char response[64];

	/* Power up SIM card */
	if (nrf_modem_at_cmd(response, sizeof(response), "AT+CFUN=41") != 0) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to activate UICC");
		return -EIO;
	}
	k_sleep(K_SECONDS(1));

	if (nrf_modem_at_scanf("AT+CIMI", "%64s\n", response) != 1) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to read IMSI");
	}
	VALIDATION_REPORT_INFO(TEST, "%16s: %s", "IMSI", response);
	if (nrf_modem_at_scanf("AT%XICCID", "%%XICCID: %64s\n", response) != 1) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to read ICCID");
	}
	VALIDATION_REPORT_INFO(TEST, "%16s: %s", "ICCID", response);

	/* Power down SIM card */
	if (nrf_modem_at_cmd(response, sizeof(response), "AT+CFUN=40") != 0) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to deactivate UICC");
		return -EIO;
	}
	return 0;
}

int infuse_validation_nrf_modem(uint8_t flags)
{
	int rc;

	VALIDATION_REPORT_INFO(TEST, "Starting");

	rc = infuse_modem_info();
	if (rc < 0) {
		goto test_end;
	}

	if (flags & VALIDATION_NRF_MODEM_SIM_CARD) {
		rc = infuse_sim_card();
	}

	if (nrf_modem_lib_shutdown() != 0) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to shutdown modem");
		if (rc == 0) {
			rc = -EIO;
		}
	};

test_end:
	if (rc == 0) {
		VALIDATION_REPORT_PASS(TEST, "PASSED");
	}
	return rc;
}
