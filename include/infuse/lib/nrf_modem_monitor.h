/**
 * @file
 * @brief
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_LIB_NRF_MODEM_MONITOR_H_
#define INFUSE_SDK_INCLUDE_INFUSE_LIB_NRF_MODEM_MONITOR_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief nrf_modem_monitor API
 * @defgroup nrf_modem_monitor_apis nrf_modem_monitor APIs
 * @{
 */

/**
 * Network registration status.
 *
 * @note Maps directly to the registration status as returned by the AT command `AT+CEREG?`.
 */
enum lte_registration_status {
	/** Not registered. UE is not currently searching for an operator to register to. */
	LTE_REGISTRATION_NOT_REGISTERED = 0,
	/** Registered, home network. */
	LTE_REGISTRATION_REGISTERED_HOME = 1,
	/**
	 * Not registered, but UE is currently trying to attach or searching for an operator to
	 * register to.
	 */
	LTE_REGISTRATION_SEARCHING = 2,
	/** Registration denied. */
	LTE_REGISTRATION_REGISTRATION_DENIED = 3,
	/** Unknown, for example out of LTE coverage. */
	LTE_REGISTRATION_UNKNOWN = 4,
	/** Registered, roaming. */
	LTE_REGISTRATION_REGISTERED_ROAMING = 5,
	/** Registered for "SMS only", home network. */
	LTE_REGISTRATION_REGISTERED_HOME_SMS_ONLY = 6,
	/** Registered for "SMS only", roaming. */
	LTE_REGISTRATION_REGISTERED_ROAMING_SMS_ONLY = 7,
	/** Attached for emergency bearer services only */
	LTE_REGISTRATION_ATTACHED_EMERGENCY_ONLY = 7,
	/** Not registered due to UICC failure (nRF91 only). */
	LTE_REGISTRATION_NRF91_UICC_FAIL = 90
};

/**
 * LTE mode.
 */
enum lte_access_technology {
	/** None. */
	LTE_ACCESS_TECH_NONE = 0,
	/** LTE-M. */
	LTE_ACCESS_TECH_LTE_M = 7,
	/** NB-IoT. */
	LTE_ACCESS_TECH_NB_IOT = 9,
};

/**
 * LTE "Radio Resource Control" state.
 */
enum lte_rrc_mode {
	/** Idle. */
	LTE_RRC_MODE_IDLE = 0,
	/** Connected. */
	LTE_RRC_MODE_CONNECTED = 1,
};

struct lte_cell {
	/** Mobile Country Code. */
	int mcc;
	/** Mobile Network Code. */
	int mnc;
	/** E-UTRAN cell ID, range 0 - @ref LTE_LC_CELL_EUTRAN_ID_MAX. */
	uint32_t id;
	/** Tracking area code. */
	uint32_t tac;
	/** EARFCN per 3GPP TS 36.101. */
	uint32_t earfcn;
	/**
	 * Timing advance decimal value in basic time units (Ts).
	 *
	 * Ts = 1/(15000 x 2048) seconds (as specified in 3GPP TS 36.211).
	 *
	 * @note Timing advance may be reported from past measurements. The parameters
	 *       @c timing_advance_meas_time and @c measurement_time can be used to evaluate if
	 *       the parameter is usable.
	 */
	uint16_t timing_advance;
	/**
	 * Cell measurement time in milliseconds, calculated from modem boot time.
	 *
	 * Range 0 - 18 446 744 073 709 551 614 ms.
	 */
	uint64_t measurement_time;
	/** Physical cell ID. */
	uint16_t phys_cell_id;
	/** Received signal power in dBm. */
	int16_t rsrp;
	/** Received signal quality in dB. */
	int8_t rsrq;
};

/** Power Saving Mode (PSM) configuration. */
struct lte_psm_cfg {
	/** Periodic Tracking Area Update interval in seconds. */
	int tau;

	/** Active-time (time from RRC idle to PSM) in seconds or @c -1 if PSM is deactivated. */
	int active_time;
};

/** eDRX configuration. */
struct lte_edrx_cfg {
	/**
	 * LTE mode for which the configuration is valid.
	 *
	 * If the mode is @ref LTE_ACCESS_TECH_NONE, access technology is not using eDRX.
	 */
	enum lte_access_technology mode;

	/** eDRX interval in seconds. */
	float edrx;

	/** Paging time window in seconds. */
	float ptw;
};

struct nrf_modem_network_state {
	enum lte_registration_status nw_reg_status;
	enum lte_access_technology lte_mode;
	enum lte_rrc_mode rrc_mode;
	struct lte_psm_cfg psm_cfg;
	struct lte_edrx_cfg edrx_cfg;
	struct lte_cell cell;
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
