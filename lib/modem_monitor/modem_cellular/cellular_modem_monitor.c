/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdlib.h>

#include <zephyr/drivers/cellular.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/toolchain.h>
#include <zephyr/pm/device_runtime.h>

#include <infuse/lib/lte_modem_monitor.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/reboot.h>

#include "../modem_monitor.h"

BUILD_ASSERT(IS_ENABLED(CONFIG_INFUSE_MODEM_MONITOR_DEFAULT_PDP_APN_SET), "Default APN required");

LOG_MODULE_DECLARE(modem_monitor, CONFIG_INFUSE_MODEM_MONITOR_LOG_LEVEL);

static struct {
	struct lte_modem_network_state network_state;
	struct kv_store_cb lte_kv_cb;
#ifdef CONFIG_INFUSE_MODEM_MONITOR_CONN_STATE_LOG
	uint8_t network_state_loggers;
#endif /* CONFIG_INFUSE_MODEM_MONITOR_CONN_STATE_LOG */
	bool at_link_dead;
} monitor;

bool lte_modem_monitor_is_at_safe(void)
{
	/* Always AT safe for now */
	return true;
}

#ifdef CONFIG_INFUSE_MODEM_MONITOR_CONN_STATE_LOG
void lte_modem_monitor_network_state_log(uint8_t tdf_logger_mask)
{
	monitor.network_state_loggers = tdf_logger_mask;
}
#endif /* CONFIG_INFUSE_MODEM_MONITOR_CONN_STATE_LOG */

int lte_modem_monitor_signal_quality(int16_t *rsrp, int8_t *rsrq, bool cached)
{
	ARG_UNUSED(cached);

	/* Rely on CELLULAR_EVENT_NETWORK_STATUS_CHANGED regularly updating the signals */
	*rsrp = monitor.network_state.cell.rsrp;
	*rsrq = monitor.network_state.cell.rsrq;

	return 0;
}

void lte_modem_monitor_network_state(struct lte_modem_network_state *state)
{
	*state = monitor.network_state;
}

static void modem_info_changed(const struct device *dev, const struct cellular_evt_modem_info *mi)
{
	KV_STRUCT_KV_STRING_VAR(65) info;

	/* Pull the information to a local buffer */
	(void)cellular_get_modem_info(dev, mi->field, info.value, sizeof(info.value));
	info.value_num = strlen(info.value) + 1;

	LOG_DBG("%d: %s", mi->field, info.value);

	/* Handle field that changed */
	switch (mi->field) {
	case CELLULAR_MODEM_INFO_IMEI: {
		struct kv_lte_modem_imei modem_imei = {
			.imei = strtoull(info.value, NULL, 10),
		};
		(void)KV_STORE_WRITE(KV_KEY_LTE_MODEM_IMEI, &modem_imei);
		/* All currently tested modems return the same value for AT+CGSN=0 and AT+CGSN=1 */
		kv_store_write(KV_KEY_LTE_MODEM_ESN, &info, 1 + info.value_num);
	} break;
	case CELLULAR_MODEM_INFO_MODEL_ID:
		kv_store_write(KV_KEY_LTE_MODEM_MODEL, &info, 1 + info.value_num);
		break;
	case CELLULAR_MODEM_INFO_MANUFACTURER:
		break;
	case CELLULAR_MODEM_INFO_FW_VERSION:
		kv_store_write(KV_KEY_LTE_MODEM_FIRMWARE_REVISION, &info, 1 + info.value_num);
		break;
	case CELLULAR_MODEM_INFO_SIM_IMSI: {
		struct kv_lte_sim_imsi sim_imsi = {
			.imsi = strtoull(info.value, NULL, 10),
		};
		if (KV_STORE_WRITE(KV_KEY_LTE_SIM_IMSI, &sim_imsi) > 0) {
			/* Print value when first saved to KV store */
			LOG_INF("IMSI: %lld", sim_imsi.imsi);
		}
	} break;
	case CELLULAR_MODEM_INFO_SIM_ICCID: {
		if (kv_store_write(KV_KEY_LTE_SIM_UICC, &info, 1 + info.value_num) > 0) {
			/* Print value when first saved to KV store */
			LOG_INF("UICC: %s", info.value);
		}
	} break;
	}
}

static void registration_status_changed(const struct device *dev,
					const struct cellular_evt_registration_status *rs)
{
	LOG_DBG("Registration status: %d", rs->status);
	monitor.network_state.nw_reg_status = rs->status;
}

static void network_status_changed(const struct device *dev,
				   const struct cellular_evt_network_status *ns)
{
	monitor.network_state.lte_mode = ns->access_tech;
	monitor.network_state.band = ns->cell.lte.band;
	monitor.network_state.cell.mcc = ns->cell.lte.mcc;
	monitor.network_state.cell.mnc = ns->cell.lte.mnc;
	monitor.network_state.cell.tac = ns->cell.lte.tac;
	monitor.network_state.cell.earfcn = ns->cell.lte.earfcn;
	monitor.network_state.cell.id = ns->cell.lte.gci;
	monitor.network_state.cell.phys_cell_id = ns->cell.lte.phys_cell_id;
	monitor.network_state.cell.rsrp = ns->cell.lte.rsrp;
	monitor.network_state.cell.rsrq = ns->cell.lte.rsrq;
}

static void periodic_script_result(const struct device *dev,
				   const struct cellular_evt_periodic_script_result *psr)
{
	static uint8_t consecutive_failures;

	if (psr->success) {
		/* Reset failure count */
		consecutive_failures = 0;
		return;
	}
	/* Only take action if multiple failures happen in a row */
	if (++consecutive_failures < 5) {
		return;
	}
	LOG_WRN("Modem AT link dead, suspending %s", dev->name);
	/* Suspend the modem */
	monitor.at_link_dead = true;
	pm_device_runtime_put(dev);
}

static void modem_suspended(const struct device *dev)
{
	/* Don't reboot if the suspension wasn't due to the periodic script failing */
	if (!monitor.at_link_dead) {
		return;
	}
	LOG_WRN("Modem suspended, rebooting");
	infuse_reboot_delayed(INFUSE_REBOOT_LTE_MODEM_FAULT, (uintptr_t)dev, 0xA700DEAD,
			      K_SECONDS(2));
}

static void modem_event_cb(const struct device *dev, enum cellular_event evt, const void *payload,
			   void *user_data)
{
	ARG_UNUSED(user_data);

	switch (evt) {
	case CELLULAR_EVENT_MODEM_INFO_CHANGED:
		modem_info_changed(dev, payload);
		break;
	case CELLULAR_EVENT_REGISTRATION_STATUS_CHANGED:
		registration_status_changed(dev, payload);
		break;
	case CELLULAR_EVENT_NETWORK_STATUS_CHANGED:
		network_status_changed(dev, payload);
		break;
	case CELLULAR_EVENT_PERIODIC_SCRIPT_RESULT:
		periodic_script_result(dev, payload);
		break;
	case CELLULAR_EVENT_MODEM_SUSPENDED:
		modem_suspended(dev);
		break;
	default:
		break;
	}
}

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

int lte_modem_monitor_init(void)
{
	struct net_if *iface = net_if_get_first_by_type(&(NET_L2_GET_NAME(PPP)));
	const struct device *modem = DEVICE_DT_GET(DT_ALIAS(modem));
	const enum cellular_event cb_events =
		CELLULAR_EVENT_MODEM_INFO_CHANGED | CELLULAR_EVENT_REGISTRATION_STATUS_CHANGED |
		CELLULAR_EVENT_NETWORK_STATUS_CHANGED | CELLULAR_EVENT_PERIODIC_SCRIPT_RESULT |
		CELLULAR_EVENT_MODEM_SUSPENDED;

#ifdef CONFIG_INFUSE_MODEM_MONITOR_DEFAULT_PDP_APN_SET
	KV_KEY_TYPE_VAR(KV_KEY_LTE_PDP_CONFIG, 32) pdp_config;
	const KV_KEY_TYPE_VAR(KV_KEY_LTE_PDP_CONFIG,
			      sizeof(CONFIG_INFUSE_MODEM_MONITOR_DEFAULT_PDP_APN)) pdp_default = {
		.apn =
			{
				.value_num =
					strlen(CONFIG_INFUSE_MODEM_MONITOR_DEFAULT_PDP_APN) + 1,
				.value = CONFIG_INFUSE_MODEM_MONITOR_DEFAULT_PDP_APN,
			},
#if defined(CONFIG_INFUSE_MODEM_MONITOR_DEFAULT_PDP_FAMILY_IPV4)
		.family = 0,
#else
#error "MODEM_CELLULAR currently hardcoded to IPV4 only"
#endif
	};

	/* Read the configured value, falling back to the default */
	(void)kv_store_read_fallback(KV_KEY_LTE_PDP_CONFIG, &pdp_config, sizeof(pdp_config),
				     &pdp_default, sizeof(pdp_default));
	LOG_INF("Using APN: %s", pdp_config.apn.value);
	if (cellular_set_apn(modem, pdp_config.apn.value) < 0) {
		LOG_ERR("Failed to set APN");
	}

#endif /* CONFIG_INFUSE_MODEM_MONITOR_DEFAULT_PDP_APN_SET */

	/* Setup KV callbacks */
	monitor.lte_kv_cb.value_changed = lte_kv_value_changed;
	kv_store_register_callback(&monitor.lte_kv_cb);

	/* Initial state */
	monitor.network_state.psm_cfg.tau = -1;
	monitor.network_state.psm_cfg.active_time = -1;
	monitor.network_state.edrx_cfg.edrx = -1.0f;
	monitor.network_state.edrx_cfg.ptw = -1.0f;
	monitor.network_state.cell.rsrp = INT16_MIN;
	monitor.network_state.cell.rsrq = INT8_MIN;
	monitor.network_state.as_rai = UINT8_MAX;
	monitor.network_state.cp_rai = UINT8_MAX;
	/* Cellular modem events */
	cellular_set_callback(modem, cb_events, modem_event_cb, NULL);
	/* Initialise generic monitor */
	modem_monitor_init(iface);
	return 0;
}

SYS_INIT(lte_modem_monitor_init, APPLICATION, 0);
