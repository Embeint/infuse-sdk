/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdlib.h>

#include <zephyr/sys/atomic.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/sys/__assert.h>

#include <infuse/work_q.h>
#include <infuse/lib/nrf_modem_monitor.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/reboot.h>
#include <infuse/task_runner/runner.h>
#include <infuse/time/epoch.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>
#include <infuse/tdf/util.h>

#include <modem/at_monitor.h>
#include <modem/pdn.h>
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>
#include <modem/modem_info.h>
#include <nrf_modem_at.h>

LOG_MODULE_REGISTER(modem_monitor, CONFIG_INFUSE_NRF_MODEM_MONITOR_LOG_LEVEL);

#define CONNECTIVITY_TIMEOUT K_SECONDS(CONFIG_INFUSE_NRF_MODEM_MONITOR_CONNECTIVITY_TIMEOUT_SEC)

#define LTE_LC_SYSTEM_MODE_DEFAULT 0xff
#define LTE_MODE_DEFAULT                                                                           \
	(IS_ENABLED(CONFIG_LTE_NETWORK_MODE_LTE_M)             ? LTE_LC_SYSTEM_MODE_LTEM           \
	 : IS_ENABLED(CONFIG_LTE_NETWORK_MODE_NBIOT)           ? LTE_LC_SYSTEM_MODE_NBIOT          \
	 : IS_ENABLED(CONFIG_LTE_NETWORK_MODE_LTE_M_GPS)       ? LTE_LC_SYSTEM_MODE_LTEM_GPS       \
	 : IS_ENABLED(CONFIG_LTE_NETWORK_MODE_NBIOT_GPS)       ? LTE_LC_SYSTEM_MODE_NBIOT_GPS      \
	 : IS_ENABLED(CONFIG_LTE_NETWORK_MODE_LTE_M_NBIOT)     ? LTE_LC_SYSTEM_MODE_LTEM_NBIOT     \
	 : IS_ENABLED(CONFIG_LTE_NETWORK_MODE_LTE_M_NBIOT_GPS) ? LTE_LC_SYSTEM_MODE_LTEM_NBIOT_GPS \
							       : LTE_LC_SYSTEM_MODE_DEFAULT)

enum {
	FLAGS_MODEM_SLEEPING = 0,
	FLAGS_CELL_CONNECTED = 1,
	/* The nRF modem can be unresponsive to AT commands while a PDN connectivity request
	 * is ongoing. As such we want to skip non-critical AT commands in this state.
	 */
	FLAGS_PDN_CONN_IN_PROGRESS = 2,
	FLAGS_IP_CONN_EXPECTED = 3,
};

static struct {
	struct nrf_modem_network_state network_state;
	/* `lte_reg_handler` runs from the system workqueue, and the modem AT commands wait forever
	 * on the response. This is problematic as the low level functions rely on malloc, which
	 * can fail. Running AT commands directly from the callback context therefore has the
	 * potential to deadlock the system workqueue, if multiple notifications occur at the same
	 * time. Workaround this by running the commands in a different context.
	 */
	struct k_work_delayable update_work;
	struct k_work signal_quality_work;
	struct net_mgmt_event_callback mgmt_iface_cb;
	struct k_work_delayable connectivity_timeout;
	struct net_if *lte_net_if;
	atomic_t flags;
	int16_t rsrp_cached;
	int8_t rsrq_cached;
#ifdef CONFIG_INFUSE_NRF_MODEM_MONITOR_CONN_STATE_LOG
	uint8_t network_state_loggers;
#endif /* CONFIG_INFUSE_NRF_MODEM_MONITOR_CONN_STATE_LOG */
} monitor;

/* Validate nRF and generic event mappings */
BUILD_ASSERT(LTE_REGISTRATION_NOT_REGISTERED ==
	     (enum lte_registration_status)LTE_LC_NW_REG_NOT_REGISTERED);
BUILD_ASSERT(LTE_REGISTRATION_REGISTERED_HOME ==
	     (enum lte_registration_status)LTE_LC_NW_REG_REGISTERED_HOME);
BUILD_ASSERT(LTE_REGISTRATION_SEARCHING == (enum lte_registration_status)LTE_LC_NW_REG_SEARCHING);
BUILD_ASSERT(LTE_REGISTRATION_REGISTRATION_DENIED ==
	     (enum lte_registration_status)LTE_LC_NW_REG_REGISTRATION_DENIED);
BUILD_ASSERT(LTE_REGISTRATION_UNKNOWN == (enum lte_registration_status)LTE_LC_NW_REG_UNKNOWN);
BUILD_ASSERT(LTE_REGISTRATION_REGISTERED_ROAMING ==
	     (enum lte_registration_status)LTE_LC_NW_REG_REGISTERED_ROAMING);
BUILD_ASSERT(LTE_REGISTRATION_NRF91_UICC_FAIL ==
	     (enum lte_registration_status)LTE_LC_NW_REG_UICC_FAIL);

BUILD_ASSERT(LTE_ACCESS_TECH_NONE == (enum lte_access_technology)LTE_LC_LTE_MODE_NONE);
BUILD_ASSERT(LTE_ACCESS_TECH_LTE_M == (enum lte_access_technology)LTE_LC_LTE_MODE_LTEM);
BUILD_ASSERT(LTE_ACCESS_TECH_NB_IOT == (enum lte_access_technology)LTE_LC_LTE_MODE_NBIOT);

BUILD_ASSERT(LTE_RRC_MODE_IDLE == (enum lte_rrc_mode)LTE_LC_RRC_MODE_IDLE);
BUILD_ASSERT(LTE_RRC_MODE_CONNECTED == (enum lte_rrc_mode)LTE_LC_RRC_MODE_CONNECTED);

bool nrf_modem_monitor_is_at_safe(void)
{
#ifdef CONFIG_SOC_NRF9160
	return true;
#else
	return !atomic_test_bit(&monitor.flags, FLAGS_PDN_CONN_IN_PROGRESS);
#endif /* CONFIG_SOC_NRF9160 */
}

#ifdef CONFIG_INFUSE_NRF_MODEM_MONITOR_CONN_STATE_LOG
void nrf_modem_monitor_network_state_log(uint8_t tdf_logger_mask)
{
	monitor.network_state_loggers = tdf_logger_mask;
}
#endif /* CONFIG_INFUSE_NRF_MODEM_MONITOR_CONN_STATE_LOG */

void nrf_modem_monitor_network_state(struct nrf_modem_network_state *state)
{
	*state = monitor.network_state;
}

static void network_info_update(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	static bool sim_card_queried;
	char plmn[9] = {0};
	int rc;

	/* This work is not time critical, run it later if the PDN connection is in progress */
	if (atomic_test_bit(&monitor.flags, FLAGS_PDN_CONN_IN_PROGRESS)) {
		infuse_work_reschedule(dwork, K_SECONDS(1));
		return;
	}

	if (!sim_card_queried) {
		KV_KEY_TYPE(KV_KEY_LTE_SIM_IMSI) sim_imsi;
		KV_STRUCT_KV_STRING_VAR(25) sim_uicc;

		/* SIM IMSI */
		rc = nrf_modem_at_scanf("AT+CIMI", "%" SCNd64 "\n", &sim_imsi.imsi);
		if (rc == 1) {
			if (KV_STORE_WRITE(KV_KEY_LTE_SIM_IMSI, &sim_imsi) > 0) {
				/* Print value when first saved to KV store */
				LOG_INF("IMSI: %lld", sim_imsi.imsi);
			}
		}
		/* SIM ICCID */
		rc = nrf_modem_at_scanf("AT%XICCID", "%%XICCID: %24s", sim_uicc.value);
		if (rc == 1) {
			sim_uicc.value_num = strlen(sim_uicc.value) + 1;
			if (kv_store_write(KV_KEY_LTE_SIM_UICC, &sim_uicc, 1 + sim_uicc.value_num) >
			    0) {
				/* Print value when first saved to KV store */
				LOG_INF("UICC: %s", sim_uicc.value);
			}
			sim_card_queried = true;
		}
	}

	if ((monitor.network_state.nw_reg_status != LTE_REGISTRATION_REGISTERED_HOME) &&
	    (monitor.network_state.nw_reg_status != LTE_REGISTRATION_REGISTERED_ROAMING)) {
		/* No cell information (except for potentially Cell ID and TAC) */
		uint32_t id = monitor.network_state.cell.id;
		uint32_t tac = monitor.network_state.cell.tac;

		memset(&monitor.network_state.cell, 0x00, sizeof(monitor.network_state.cell));

		monitor.network_state.cell.id = id;
		monitor.network_state.cell.tac = tac;
		monitor.network_state.psm_cfg.tau = -1;
		monitor.network_state.psm_cfg.active_time = -1;
		monitor.network_state.edrx_cfg.edrx = -1.0f;
		monitor.network_state.edrx_cfg.ptw = -1.0f;
		goto state_logging;
	}

	/* Query state from the modem */
	rc = nrf_modem_at_scanf("AT%XMONITOR",
				"%%XMONITOR: "
				"%*u,"         /* <reg_status>: ignored */
				"%*[^,],"      /* <full_name>: ignored */
				"%*[^,],"      /* <short_name>: ignored */
				"%9[^,],"      /* <plmn> */
				"%*[^,],"      /* <tac>: ignored */
				"%*d,"         /* <AcT>: ignored */
				"%" SCNu16 "," /* <band> */
				"%*[^,],"      /* <cell_id>: ignored */
				"%" SCNu16 "," /* <phys_cell_id> */
				"%" SCNu32 "," /* <EARFCN> */
				,
				plmn, &monitor.network_state.band,
				&monitor.network_state.cell.phys_cell_id,
				&monitor.network_state.cell.earfcn);
	if (rc == 4) {
		/* Parse MCC and MNC. The PLMN string is a 5 or 6 digit number surrounded by quotes.
		 * The first 3 numeric characters are the MCC (Mobile Country Code).
		 * The next 2 or 3 numeric characters are the MNC (Mobile Network Code).
		 * atoi() ignores trailing non-numeric characters.
		 * Read the trailing MNC first (always starts at offset 4).
		 * Then set the first MNC digit to \x00 so we can read the MCC.
		 */
		monitor.network_state.cell.mnc = atoi(plmn + 4);
		plmn[4] = '\x00';
		monitor.network_state.cell.mcc = atoi(plmn + 1);
	} else {
		/* Try again shortly */
		infuse_work_reschedule(dwork, K_SECONDS(1));
		return;
	}

state_logging:
#ifdef CONFIG_INFUSE_NRF_MODEM_MONITOR_CONN_STATE_LOG
	if (monitor.network_state_loggers) {
		struct tdf_lte_conn_status tdf;
		int16_t rsrp;
		int8_t rsrq;

		/* Query signal strengths (other state already queried above) */
		(void)nrf_modem_monitor_signal_quality(&rsrp, &rsrq, true);
		/* Convert to TDF */
		tdf_lte_conn_status_from_monitor(&monitor.network_state, &tdf, rsrp, rsrq);
		/* Add to specified loggers */
		TDF_DATA_LOGGER_LOG(monitor.network_state_loggers, TDF_LTE_CONN_STATUS,
				    epoch_time_now(), &tdf);
	}
#endif /* CONFIG_INFUSE_NRF_MODEM_MONITOR_CONN_STATE_LOG */
}

static void signal_quality_update(struct k_work *work)
{
	int16_t rsrp;
	int8_t rsrq;

	(void)nrf_modem_monitor_signal_quality(&rsrp, &rsrq, false);
}

int nrf_modem_monitor_signal_quality(int16_t *rsrp, int8_t *rsrq, bool cached)
{
	bool sleeping = atomic_test_bit(&monitor.flags, FLAGS_MODEM_SLEEPING);
	bool connected = atomic_test_bit(&monitor.flags, FLAGS_CELL_CONNECTED);
	bool pdn_in_progress = atomic_test_bit(&monitor.flags, FLAGS_PDN_CONN_IN_PROGRESS);
	uint8_t rsrp_idx, rsrq_idx;
	int rc;

	*rsrp = cached ? monitor.rsrp_cached : INT16_MIN;
	*rsrq = cached ? monitor.rsrq_cached : INT8_MIN;

	/* If modem is sleeping or not connected to a cell, signal quality polling will fail */
	if (sleeping || !connected || pdn_in_progress) {
		LOG_DBG("Sleeping: %d Connected: %d PDN: %d", sleeping, connected, pdn_in_progress);
		return 0;
	}

	/* Query state from the modem */
	rc = nrf_modem_at_scanf("AT+CESQ", "+CESQ: %*d,%*d,%*d,%*d,%" SCNu8 ",%" SCNu8, &rsrp_idx,
				&rsrq_idx);
	if (rc != 2) {
		return -EAGAIN;
	}

	/* Convert from index to physical units if known */
	if (rsrp_idx != 255) {
		*rsrp = RSRP_IDX_TO_DBM(rsrp_idx);
		monitor.rsrp_cached = *rsrp;
	}
	if (rsrq_idx != 255) {
		*rsrq = RSRQ_IDX_TO_DB(rsrq_idx);
		monitor.rsrq_cached = *rsrq;
	}
	return 0;
}

int nrf_modem_monitor_connectivity_stats(int *tx_kbytes, int *rx_kbytes)
{
	int rc;

	if (atomic_test_bit(&monitor.flags, FLAGS_PDN_CONN_IN_PROGRESS)) {
		return -EAGAIN;
	}
	rc = nrf_modem_at_scanf("AT%XCONNSTAT?", "%%XCONNSTAT: %*d,%*d,%d,%d,%*d,%*d", tx_kbytes,
				rx_kbytes);
	return rc == 2 ? 0 : -EIO;
}

static void lte_reg_handler(const struct lte_lc_evt *const evt)
{
	switch (evt->type) {
	case LTE_LC_EVT_NW_REG_STATUS:
		LOG_DBG("NW_REG_STATUS");
		LOG_DBG("  STATUS: %d", evt->nw_reg_status);
		monitor.network_state.nw_reg_status = evt->nw_reg_status;
		/* Handle the connectivity watchdog */
		if ((evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME) ||
		    (evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_ROAMING)) {
			atomic_set_bit(&monitor.flags, FLAGS_IP_CONN_EXPECTED);
			k_work_reschedule(&monitor.connectivity_timeout, CONNECTIVITY_TIMEOUT);
		} else {
			atomic_clear_bit(&monitor.flags, FLAGS_IP_CONN_EXPECTED);
			k_work_cancel_delayable(&monitor.connectivity_timeout);
		}
		/* Request update of knowledge of network info */
		infuse_work_reschedule(&monitor.update_work, K_NO_WAIT);
		break;
	case LTE_LC_EVT_PSM_UPDATE:
		LOG_DBG("PSM_UPDATE");
		LOG_DBG("     TAU: %d", evt->psm_cfg.tau);
		LOG_DBG("  ACTIVE: %d", evt->psm_cfg.active_time);
		monitor.network_state.psm_cfg.tau = evt->psm_cfg.tau;
		monitor.network_state.psm_cfg.active_time = evt->psm_cfg.active_time;
		break;
	case LTE_LC_EVT_EDRX_UPDATE:
		LOG_DBG("EDRX_UPDATE");
		LOG_DBG("    Mode: %d", evt->edrx_cfg.mode);
		LOG_DBG("     PTW: %d", (int)evt->edrx_cfg.ptw);
		LOG_DBG("Interval: %d", (int)evt->edrx_cfg.edrx);
		monitor.network_state.edrx_cfg.mode = evt->edrx_cfg.mode;
		monitor.network_state.edrx_cfg.edrx = evt->edrx_cfg.edrx;
		monitor.network_state.edrx_cfg.ptw = evt->edrx_cfg.ptw;
		break;
	case LTE_LC_EVT_RRC_UPDATE:
		LOG_DBG("RRC_UPDATE");
		LOG_DBG("   State: %s", evt->rrc_mode == LTE_LC_RRC_MODE_IDLE ? "Idle" : "Active");
		monitor.network_state.rrc_mode = evt->rrc_mode;
		if (evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED) {
			/* Update cached knowledge of signal strength */
			infuse_work_submit(&monitor.signal_quality_work);
		}
		break;
	case LTE_LC_EVT_CELL_UPDATE:
		LOG_DBG("CELL_UPDATE");
		LOG_DBG("     TAC: %d", evt->cell.tac);
		LOG_DBG("      ID: %d", evt->cell.id);
		/* Set cell info */
		monitor.network_state.cell.tac = evt->cell.tac;
		monitor.network_state.cell.id = evt->cell.id;
		/* Reset cached signal strength */
		monitor.rsrp_cached = INT16_MIN;
		monitor.rsrq_cached = INT8_MIN;
		/* Set cell connected flag */
		atomic_set_bit_to(&monitor.flags, FLAGS_CELL_CONNECTED,
				  evt->cell.id <= LTE_LC_CELL_EUTRAN_ID_MAX);
		/* Request update of knowledge of network info */
		infuse_work_reschedule(&monitor.update_work, K_NO_WAIT);
		/* Update cached knowledge of signal strength */
		infuse_work_submit(&monitor.signal_quality_work);
		break;
	case LTE_LC_EVT_LTE_MODE_UPDATE:
		LOG_DBG("LTE_MODE_UPDATE");
		LOG_DBG("    Mode: %d", evt->lte_mode);
		monitor.network_state.lte_mode = evt->lte_mode;
		break;
	case LTE_LC_EVT_MODEM_SLEEP_ENTER:
		LOG_DBG("MODEM_SLEEP_ENTER");
		LOG_DBG("    Type: %d", evt->modem_sleep.type);
		LOG_DBG("     Dur: %lld", evt->modem_sleep.time);
		atomic_set_bit(&monitor.flags, FLAGS_MODEM_SLEEPING);
		break;
	case LTE_LC_EVT_MODEM_SLEEP_EXIT:
		LOG_DBG("MODEM_SLEEP_EXIT");
		LOG_DBG("    Type: %d", evt->modem_sleep.type);
		atomic_clear_bit(&monitor.flags, FLAGS_MODEM_SLEEPING);
		break;
	case LTE_LC_EVT_MODEM_EVENT:
		LOG_DBG("MODEM_EVENT");
		LOG_DBG("   Event: %d", evt->modem_evt);
		break;
	default:
		LOG_DBG("LTE EVENT: %d", evt->type);
		break;
	}
}

NRF_MODEM_LIB_ON_INIT(infuse_cfun_hook, infuse_modem_init, NULL);

static struct kv_store_cb lte_kv_cb;

static void lte_kv_value_changed(uint16_t key, const void *data, size_t data_len, void *user_ctx)
{
	if (key == KV_KEY_LTE_PDP_CONFIG) {
		LOG_INF("Rebooting to apply updated %s configuration", "PDP");
	} else if (key == KV_KEY_LTE_NETWORKING_MODES) {
		LOG_INF("Rebooting to apply updated %s configuration", "LTE mode");
	} else {
		return;
	}

#ifdef CONFIG_INFUSE_REBOOT
	/* PDP contexts can only be changed when the PDN is inactive.
	 * Networking modes can only be changed while LTE is disabled.
	 * The easiest way to achieve this is to reboot the application and let
	 * infuse_modem_init configure it appropriately.
	 */
	infuse_reboot_delayed(INFUSE_REBOOT_CFG_CHANGE, KV_KEY_LTE_PDP_CONFIG, data_len,
			      K_SECONDS(2));
#else
	LOG_WRN("No reboot support!");
#endif /* CONFIG_INFUSE_REBOOT */
}

static void infuse_modem_init(int ret, void *ctx)
{
	KV_STRUCT_KV_STRING_VAR(65) modem_info = {0};
	KV_KEY_TYPE(KV_KEY_LTE_MODEM_IMEI) modem_imei;
	static bool modem_info_stored;
	uint8_t val;
	int rc;

	/* Ensure modem commands don't block forever */
	rc = nrf_modem_at_sem_timeout_set(CONFIG_INFUSE_NRF_MODEM_MONITOR_AT_TIMEOUT_MS);
	__ASSERT_NO_MSG(rc == 0);

#ifndef CONFIG_SOC_NRF9160
	/* Enable notifications of BIP events */
	rc = nrf_modem_at_printf("%s", "AT%USATEV=1");
	__ASSERT_NO_MSG(rc == 0);
#endif /* !CONFIG_SOC_NRF9160 */

	/* Enable connectivity stats */
	rc = nrf_modem_at_printf("%s", "AT%XCONNSTAT=1");
	__ASSERT_NO_MSG(rc == 0);

#ifdef CONFIG_KV_STORE_KEY_LTE_PDP_CONFIG
	KV_KEY_TYPE_VAR(KV_KEY_LTE_PDP_CONFIG, 32) pdp_config;

#ifdef CONFIG_INFUSE_NRF_MODEM_MONITOR_DEFAULT_PDP_APN_SET
	const KV_KEY_TYPE_VAR(
		KV_KEY_LTE_PDP_CONFIG,
		sizeof(CONFIG_INFUSE_NRF_MODEM_MONITOR_DEFAULT_PDP_APN)) pdp_default = {
		.apn =
			{
				.value_num =
					strlen(CONFIG_INFUSE_NRF_MODEM_MONITOR_DEFAULT_PDP_APN) + 1,
				.value = CONFIG_INFUSE_NRF_MODEM_MONITOR_DEFAULT_PDP_APN,
			},
#if defined(CONFIG_INFUSE_NRF_MODEM_MONITOR_DEFAULT_PDP_FAMILY_IPV4)
		.family = PDN_FAM_IPV4,
#elif defined(CONFIG_INFUSE_NRF_MODEM_MONITOR_DEFAULT_PDP_FAMILY_IPV6)
		.family = PDN_FAM_IPV6,
#elif defined(CONFIG_INFUSE_NRF_MODEM_MONITOR_DEFAULT_PDP_FAMILY_IPV4V6)
		.family = PDN_FAM_IPV4V6,
#elif defined(CONFIG_INFUSE_NRF_MODEM_MONITOR_DEFAULT_PDP_FAMILY_NON_IP)
		.family = PDN_FAM_NONIP,
#else
#error "Unknown protocol family"
#endif
	};

	/* Read the configured value, falling back to the default */
	rc = kv_store_read_fallback(KV_KEY_LTE_PDP_CONFIG, &pdp_config, sizeof(pdp_config),
				    &pdp_default, sizeof(pdp_default));
#else
	/* Read the configured value */
	rc = kv_store_read(KV_KEY_LTE_PDP_CONFIG, &pdp_config, sizeof(pdp_config));
	pdp_config.apn.value[sizeof(pdp_config.apn.value) - 1] = '\0';
#endif /* CONFIG_INFUSE_NRF_MODEM_MONITOR_DEFAULT_PDP_APN_SET */

	/* If a PDP configuration has been set */
	if ((rc > 0) && (strlen(pdp_config.apn.value) > 0)) {
		LOG_DBG("PDP configuration: %d %s", pdp_config.family, pdp_config.apn.value);
		rc = pdn_ctx_configure(0, pdp_config.apn.value, pdp_config.family, NULL);
		if (rc < 0) {
			LOG_ERR("Failed to request PDP configuration (%d)", rc);
			/* Remove the invalid configuration */
			(void)kv_store_delete(KV_KEY_LTE_PDP_CONFIG);
		}
	}

#endif /* CONFIG_KV_STORE_KEY_LTE_PDP_CONFIG */

#ifdef CONFIG_KV_STORE_KEY_LTE_NETWORKING_MODES
	const KV_KEY_TYPE(KV_KEY_LTE_NETWORKING_MODES) modes_default = {
		.modes = LTE_MODE_DEFAULT,
		.prefer = CONFIG_LTE_MODE_PREFERENCE_VALUE,
	};
	KV_KEY_TYPE(KV_KEY_LTE_NETWORKING_MODES) modes;

	/* Read the requested LTE networking modes and set */
	rc = KV_STORE_READ_FALLBACK(KV_KEY_LTE_NETWORKING_MODES, &modes, &modes_default);
	if (rc == sizeof(modes)) {
		rc = lte_lc_system_mode_set(modes.modes, modes.prefer);
		if (rc != 0) {
			LOG_WRN("Failed to set configurated LTE modes (%d, %d)", modes.modes,
				modes.prefer);
		}
	} else {
		LOG_WRN("Failed to read LTE modes, will use default");
	}
#endif /* CONFIG_KV_STORE_KEY_LTE_NETWORKING_MODES */

	if (lte_kv_cb.value_changed == NULL) {
		/* Setup callback on first run */
		lte_kv_cb.value_changed = lte_kv_value_changed;
		kv_store_register_callback(&lte_kv_cb);
	}

	if (modem_info_stored) {
		return;
	}
	/* Model identifier */
	nrf_modem_at_scanf("AT+CGMM", "%64s\n", modem_info.value);
	modem_info.value_num = strlen(modem_info.value) + 1;
	(void)kv_store_write(KV_KEY_LTE_MODEM_MODEL, &modem_info, 1 + modem_info.value_num);
	/* Modem firmware revision */
	nrf_modem_at_scanf("AT+CGMR", "%64s\n", modem_info.value);
	modem_info.value_num = strlen(modem_info.value) + 1;
	(void)kv_store_write(KV_KEY_LTE_MODEM_FIRMWARE_REVISION, &modem_info,
			     1 + modem_info.value_num);
	/* Modem ESN */
	nrf_modem_at_scanf("AT+CGSN=0", "%64s\n", modem_info.value);
	modem_info.value_num = strlen(modem_info.value) + 1;
	(void)kv_store_write(KV_KEY_LTE_MODEM_ESN, &modem_info, 1 + modem_info.value_num);
	/* Modem IMEI */
	nrf_modem_at_scanf("AT+CGSN=1", "+CGSN: \"%" SCNd64 "\"\n", &modem_imei.imei);
	(void)KV_STORE_WRITE(KV_KEY_LTE_MODEM_IMEI, &modem_imei);
	/* Modem info has been stored */
	modem_info_stored = true;

	/* Set default %XDATAPRFL value */
	val = CONFIG_INFUSE_NRF_MODEM_DATA_PROFILE_DEFAULT;
	rc = nrf_modem_at_printf("AT%%XDATAPRFL=%d", val);
	if (rc < 0) {
		LOG_ERR("AT%%XDATAPRFL=%d (%d)", val, rc);
	}

	/* Set default %REDMOB value */
	val = CONFIG_INFUSE_NRF_MODEM_MONITOR_MOBILITY_VALUE;
	rc = nrf_modem_at_printf("AT%%REDMOB=%d", val);
	if (rc < 0) {
		LOG_ERR("AT%%REDMOB=%d (%d)", val, rc);
	}
}

#ifndef CONFIG_SOC_NRF9160

/* AT monitor for USAT notifications */
AT_MONITOR(usat_notification, "%USATEV: BIP", usat_mon);

static void usat_mon(const char *notif)
{
	if (strstr(notif, "Connecting") != NULL) {
		atomic_set_bit(&monitor.flags, FLAGS_PDN_CONN_IN_PROGRESS);
	} else {
		atomic_clear_bit(&monitor.flags, FLAGS_PDN_CONN_IN_PROGRESS);
	}
	/* Output the BIP notification, minus the newline */
	LOG_INF("%.*s", strlen(notif) - 1, notif);
}

#endif /* !CONFIG_SOC_NRF9160 */

void lte_net_if_modem_fault_app_handler(struct nrf_modem_fault_info *fault_info)
{
#ifdef CONFIG_INFUSE_REBOOT
	/* Handling any fault properly is uncertain, safest option is to trigger a reboot */
	LOG_ERR("Modem fault, rebooting in 2 seconds...");
	infuse_reboot_delayed(INFUSE_REBOOT_LTE_MODEM_FAULT, fault_info->program_counter,
			      fault_info->reason, K_SECONDS(2));
#else
	LOG_ERR("Modem fault, no reboot support!");
#endif /* CONFIG_INFUSE_REBOOT */
}

static void connectivity_timeout(struct k_work *work)
{
	if (!atomic_test_bit(&monitor.flags, FLAGS_IP_CONN_EXPECTED)) {
		/* Network registration was lost before interface state callback occurred */
		return;
	}

	/* Interface has failed to gain IP connectivity, the safest option is to reboot */
#ifdef CONFIG_INFUSE_REBOOT
	LOG_ERR("Networking connectivity failed, rebooting in 2 seconds...");
	infuse_reboot_delayed(INFUSE_REBOOT_SW_WATCHDOG, (uintptr_t)connectivity_timeout,
			      CONFIG_INFUSE_NRF_MODEM_MONITOR_CONNECTIVITY_TIMEOUT_SEC,
			      K_SECONDS(2));
#else
	LOG_ERR("Networking connectivity failed, no reboot support!");
#endif /* CONFIG_INFUSE_REBOOT */
}

static void iface_state_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
				struct net_if *iface)
{
	if (iface != monitor.lte_net_if) {
		return;
	}
	if (mgmt_event == NET_EVENT_IF_UP) {
		/* Interface is UP, cancel the timeout */
		k_work_cancel_delayable(&monitor.connectivity_timeout);
	} else if (mgmt_event == NET_EVENT_IF_DOWN) {
		/* Interface is DOWN, restart the timeout */
		k_work_reschedule(&monitor.connectivity_timeout, CONNECTIVITY_TIMEOUT);
	}
}

int nrf_modem_monitor_init(void)
{
	k_work_init_delayable(&monitor.update_work, network_info_update);
	k_work_init(&monitor.signal_quality_work, signal_quality_update);
	/* Initial state */
	monitor.network_state.psm_cfg.tau = -1;
	monitor.network_state.psm_cfg.active_time = -1;
	monitor.network_state.edrx_cfg.edrx = -1.0f;
	monitor.network_state.edrx_cfg.ptw = -1.0f;
	monitor.rsrp_cached = INT16_MIN;
	monitor.rsrq_cached = INT8_MIN;
	/* Network connectivity timeout handler */
	monitor.lte_net_if = net_if_get_first_by_type(&(NET_L2_GET_NAME(OFFLOADED_NETDEV)));
	__ASSERT_NO_MSG(monitor.lte_net_if != NULL);
	k_work_init_delayable(&monitor.connectivity_timeout, connectivity_timeout);
	net_mgmt_init_event_callback(&monitor.mgmt_iface_cb, iface_state_handler,
				     NET_EVENT_IF_UP | NET_EVENT_IF_DOWN);
	net_mgmt_add_event_callback(&monitor.mgmt_iface_cb);
	/* Register handler */
	lte_lc_register_handler(lte_reg_handler);
	return 0;
}

SYS_INIT(nrf_modem_monitor_init, APPLICATION, 0);
