/**
 * @file
 * @brief
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 *
 * @details
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_LIB_NRF_MODEM_MONITOR_H_
#define INFUSE_SDK_INCLUDE_INFUSE_LIB_NRF_MODEM_MONITOR_H_

#include <stdbool.h>

#include <modem/lte_lc.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief nrf_modem_monitor API
 * @defgroup nrf_modem_monitor_apis nrf_modem_monitor APIs
 * @{
 */

struct nrf_modem_network_state {
	enum lte_lc_nw_reg_status nw_reg_status;
	enum lte_lc_lte_mode lte_mode;
	enum lte_lc_rrc_mode rrc_mode;
	struct lte_lc_psm_cfg psm_cfg;
	struct lte_lc_edrx_cfg edrx_cfg;
	struct lte_lc_cell cell;
	uint16_t band;
};

/**
 * @brief Get current network state
 *
 * @param state Network state
 */
void nrf_modem_monitor_network_state(struct nrf_modem_network_state *state);

/**
 * @brief Get current signal quality
 *
 * @param rsrp Reference signal received power
 * @param rsrq Reference signal received quality
 * @param cached Return cached signal quality from previous run if modem can
 *               no longer determine the parameters. Cached values are reset
 *               when the cell tower changes
 *
 * @retval 0 on success
 * @retval -errno on error
 */
int nrf_modem_monitor_signal_quality(int16_t *rsrp, int8_t *rsrq, bool cached);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_LIB_NRF_MODEM_MONITOR_H_ */
