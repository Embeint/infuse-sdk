/**
 * @file
 * @brief
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
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
 * @brief Query whether it is currently safe to send AT commands
 *
 * The nRF modem can be unresponsive to AT commands while a PDN connectivity request
 * is ongoing. Non-critical AT commands should be skipped in this state.
 *
 * This command will always return true on nRF9160 devices due to legacy modem firmware
 * limitations (No ability to monitor PDN status).
 *
 * @retval true AT command interface can be used
 * @retval false AT command interface should not be used
 */
bool nrf_modem_monitor_is_at_safe(void);

/**
 * @brief Get current network state
 *
 * @param state Network state
 */
void nrf_modem_monitor_network_state(struct nrf_modem_network_state *state);

/**
 * @brief Configure the modem monitor to automatically log network state changes
 *
 * Logs @ref TDF_LTE_CONN_STATUS on registration status and cell changes.
 *
 * @param tdf_logger_mask TDF data logger mask to log state changes to
 */
void nrf_modem_monitor_network_state_log(uint8_t tdf_logger_mask);

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
 * @brief Get current connectivity statistics
 *
 * @param tx_kbytes Storage for number of kilobytes transmitted.
 * @param rx_kbytes Storage for number of kilobytes received.
 *
 * @retval 0 on success
 * @retval -errno on error
 */
int nrf_modem_monitor_connectivity_stats(int *tx_kbytes, int *rx_kbytes);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_LIB_NRF_MODEM_MONITOR_H_ */
