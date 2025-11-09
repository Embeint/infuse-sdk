/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>

#include <infuse/validation/core.h>
#include <infuse/validation/nrf_modem.h>

#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>
#include <modem/modem_info.h>
#include <nrf_modem_at.h>

#define TEST "MODEM"

static K_SEM_DEFINE(lte_cell_scan_complete, 0, 1);
static int gci_cells_found;
static bool cell_scan_complete;

static int infuse_modem_info(void)
{
	char storage[64];
	uint64_t imei;

	/* Model identifier */
	if (nrf_modem_at_scanf("AT+CGMM", "%64s\n", storage) != 1) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to read model identifier");
		return -EIO;
	}
	VALIDATION_REPORT_VALUE(TEST, "MODEL", "%s", storage);

	/* Modem ESN */
	if (nrf_modem_at_scanf("AT+CGSN=0", "%64s\n", storage) != 1) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to read ESN");
		return -EIO;
	}
	VALIDATION_REPORT_VALUE(TEST, "ESN", "%s", storage);

	/* Modem IMEI */
	if (nrf_modem_at_scanf("AT+CGSN=1", "+CGSN: \"%" SCNd64 "\"\n", &imei) != 1) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to read IMEI");
		return -EIO;
	}
	VALIDATION_REPORT_VALUE(TEST, "IMEI", "%llu", imei);

	/* Modem firmware revision */
	if (nrf_modem_at_scanf("AT+CGMR", "%64s\n", storage) != 1) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to read firmware version");
		return -EIO;
	}
	VALIDATION_REPORT_VALUE(TEST, "FW_VERSION", "%s", storage);

	return 0;
}

static int infuse_sim_card(void)
{
	char response[64];
	int rc = 0;

	/* Power up SIM card */
	rc = lte_lc_func_mode_set(LTE_LC_FUNC_MODE_ACTIVATE_UICC);
	if (rc != 0) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to activate UICC");
		return -EIO;
	}
	k_sleep(K_SECONDS(1));

	if (nrf_modem_at_scanf("AT+CIMI", "%64s\n", response) != 1) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to read IMSI");
		rc = -EIO;
	}
	VALIDATION_REPORT_VALUE(TEST, "SIM_IMSI", "%s", response);

	if (nrf_modem_at_scanf("AT%XICCID", "%%XICCID: %64s\n", response) != 1) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to read ICCID");
		rc = -EIO;
	}
	VALIDATION_REPORT_VALUE(TEST, "SIM_ICCID", "%s", response);

	/* Power down SIM card */
	if (lte_lc_func_mode_set(LTE_LC_FUNC_MODE_DEACTIVATE_UICC) != 0) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to deactivate UICC");
		rc = -EIO;
	}

	return rc;
}

void network_scan_lte_handler(const struct lte_lc_evt *const evt)
{
	const struct lte_lc_cells_info *info;

	if (evt->type != LTE_LC_EVT_NEIGHBOR_CELL_MEAS) {
		return;
	}
	if (cell_scan_complete) {
		/* lte_lc_neighbor_cell_measurement_cancel() schedules a callback to
		 * run after 2 seconds with no cells to cover the case where the scanning
		 * has not yet started. We don't want to print no cells found, since we
		 * have already printed the results.
		 */
		return;
	}
	info = &evt->cells_info;

	gci_cells_found = info->gci_cells_count;
	VALIDATION_REPORT_INFO(TEST, "Found %d global cells", info->gci_cells_count);
	for (int i = 0; i < info->gci_cells_count; i++) {
		struct lte_lc_cell *cell = &info->gci_cells[i];

		VALIDATION_REPORT_INFO(TEST, "CELL %d: ID %d EARFCN %d RSRP %d dBm RSRQ %d dBm", i,
				       cell->id, cell->earfcn, (int)RSRP_IDX_TO_DBM(cell->rsrp),
				       (int)RSRQ_IDX_TO_DB(cell->rsrq));
	}

	/* Notify the scan has completed */
	k_sem_give(&lte_cell_scan_complete);
	cell_scan_complete = true;
}

static int network_cell_scan(void)
{
	struct lte_lc_ncellmeas_params ncellmeas_params = {
		.search_type = LTE_LC_NEIGHBOR_SEARCH_TYPE_GCI_EXTENDED_COMPLETE,
		.gci_count = 2,
	};
	int rc;

	/* Register for the events */
	lte_lc_register_handler(network_scan_lte_handler);

	/* Enable the LTE portion of the modem */
	rc = lte_lc_func_mode_set(LTE_LC_FUNC_MODE_ACTIVATE_LTE);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to enable RX mode (%d)", rc);
		return rc;
	}

	VALIDATION_REPORT_INFO(TEST, "Starting cell scan");

	rc = lte_lc_neighbor_cell_measurement(&ncellmeas_params);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to start cell scan (%d)", rc);
		goto cleanup;
	}

	rc = k_sem_take(&lte_cell_scan_complete,
			K_SECONDS(CONFIG_INFUSE_VALIDATE_NRF_MODEM_GCI_SEARCH_TIMEOUT));
	if (rc < 0) {
		VALIDATION_REPORT_INFO(TEST, "Terminating cell scan");
		lte_lc_neighbor_cell_measurement_cancel();
		/* Callback runs on cancel */
		k_sem_take(&lte_cell_scan_complete, K_FOREVER);
	}

	/* Validate number of cells found */
	rc = gci_cells_found >= CONFIG_INFUSE_VALIDATE_NRF_MODEM_GCI_MIN_CELL ? 0 : -EAGAIN;

cleanup:
	(void)lte_lc_func_mode_set(LTE_LC_FUNC_MODE_DEACTIVATE_LTE);
	return rc;
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

	if ((rc == 0) && (flags & VALIDATION_NRF_MODEM_LTE_SCAN)) {
		rc = network_cell_scan();
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
