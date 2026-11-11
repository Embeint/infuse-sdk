/**
 * @file
 * @brief LTE modem monitor TDF helpers
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_LIB_LTE_MODEM_MONITOR_TDF_H_
#define INFUSE_SDK_INCLUDE_INFUSE_LIB_LTE_MODEM_MONITOR_TDF_H_

#include <stdint.h>

#include <infuse/lib/lte_modem_monitor.h>
#include <infuse/tdf/definitions.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LTE modem monitor TDF helpers
 * @defgroup lte_modem_monitor_tdf_apis LTE modem monitor TDF helpers
 * @ingroup lte_modem_monitor_apis
 * @{
 */

/**
 * @brief Populate the LTE connection status TDF from modem monitor information
 *
 * @param network_state Modem monitor network state
 * @param tdf TDF state structure
 * @param rsrp Reference signal received power (dBm) (INT16_MIN if unknown)
 * @param rsrq Reference signal received quality (dB) (INT8_MIN if unknown)
 */
static inline void
tdf_lte_conn_status_from_monitor(const struct lte_modem_network_state *network_state,
				 struct tdf_lte_conn_status *tdf, int16_t rsrp, int8_t rsrq)
{
	tdf->cell.mcc = network_state->cell.mcc;
	tdf->cell.mnc = network_state->cell.mnc;
	tdf->cell.tac = network_state->cell.tac;
	tdf->cell.eci = network_state->cell.id;
	tdf->status = network_state->nw_reg_status;
	tdf->tech = network_state->lte_mode;
	tdf->earfcn = network_state->cell.earfcn;
	tdf->rsrq = rsrq;
	tdf->rsrp = UINT8_MAX;
	if (rsrp != INT16_MIN) {
		tdf->rsrp = 0 - rsrp;
	}
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_LIB_LTE_MODEM_MONITOR_TDF_H_ */
