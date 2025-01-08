/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/zbus/zbus.h>

#include <infuse/zbus/channels.h>

#include <memfault/metrics/metrics.h>
#include <memfault/metrics/connectivity.h>
#include <memfault/metrics/platform/battery.h>
#include <memfault/metrics/platform/connectivity.h>

#ifdef CONFIG_MEMFAULT_INFUSE_METRICS_BATTERY

INFUSE_ZBUS_CHAN_DECLARE(INFUSE_ZBUS_CHAN_BATTERY);

int memfault_platform_get_stateofcharge(sMfltPlatformBatterySoc *soc)
{
	INFUSE_ZBUS_TYPE(INFUSE_ZBUS_CHAN_BATTERY) battery;

	zbus_chan_read(INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY), &battery, K_FOREVER);

	soc->soc = battery.soc;
	soc->discharging = true;
	return 0;
}

#endif /* CONFIG_MEMFAULT_INFUSE_METRICS_BATTERY */

#ifdef CONFIG_MEMFAULT_INFUSE_NRF_MODEM

#include <infuse/lib/nrf_modem_monitor.h>

#include <modem/lte_lc.h>

static void memfault_lte_event_handler(const struct lte_lc_evt *const evt)
{
	static __maybe_unused bool connected;

	switch (evt->type) {
	case LTE_LC_EVT_NW_REG_STATUS:
		switch (evt->nw_reg_status) {
		case LTE_LC_NW_REG_REGISTERED_HOME:
		case LTE_LC_NW_REG_REGISTERED_ROAMING:
#ifdef CONFIG_MEMFAULT_INFUSE_METRICS_CONNECTIVITY_NRF_MODEM
			memfault_metrics_connectivity_connected_state_change(
				kMemfaultMetricsConnectivityState_Connected);
#endif
#ifdef CONFIG_MEMFAULT_INFUSE_METRICS_NRF_MODEM
			connected = true;
			MEMFAULT_METRIC_TIMER_STOP(ncs_lte_time_to_connect_ms);
#endif
			break;
		case LTE_LC_NW_REG_NOT_REGISTERED:
		case LTE_LC_NW_REG_SEARCHING:
		case LTE_LC_NW_REG_REGISTRATION_DENIED:
		case LTE_LC_NW_REG_UNKNOWN:
		case LTE_LC_NW_REG_UICC_FAIL:
#ifdef CONFIG_MEMFAULT_INFUSE_METRICS_CONNECTIVITY_NRF_MODEM
			memfault_metrics_connectivity_connected_state_change(
				kMemfaultMetricsConnectivityState_ConnectionLost);
#endif
#ifdef CONFIG_MEMFAULT_INFUSE_METRICS_NRF_MODEM
			if (connected) {
				MEMFAULT_METRIC_ADD(ncs_lte_connection_loss_count, 1);
				MEMFAULT_METRIC_TIMER_START(ncs_lte_time_to_connect_ms);
				connected = false;
			}
#endif
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void memfault_lte_mode_cb(enum lte_lc_func_mode mode, void *ctx)
{
	switch (mode) {
	case LTE_LC_FUNC_MODE_NORMAL:
	case LTE_LC_FUNC_MODE_ACTIVATE_LTE:
#ifdef CONFIG_MEMFAULT_INFUSE_METRICS_CONNECTIVITY_NRF_MODEM
		memfault_metrics_connectivity_connected_state_change(
			kMemfaultMetricsConnectivityState_Started);
#endif
#ifdef CONFIG_MEMFAULT_INFUSE_METRICS_NRF_MODEM
		MEMFAULT_METRIC_TIMER_START(ncs_lte_time_to_connect_ms);
#endif
		break;
	case LTE_LC_FUNC_MODE_POWER_OFF:
	case LTE_LC_FUNC_MODE_OFFLINE:
	case LTE_LC_FUNC_MODE_DEACTIVATE_LTE:
#ifdef CONFIG_MEMFAULT_INFUSE_METRICS_CONNECTIVITY_NRF_MODEM
		memfault_metrics_connectivity_connected_state_change(
			kMemfaultMetricsConnectivityState_Stopped);
#endif
		break;
	default:
		break;
	}
}

LTE_LC_ON_CFUN(memfault_lte_mode_cb, memfault_lte_mode_cb, NULL);

void memfault_platform_metrics_connectivity_boot(void)
{
	lte_lc_register_handler(memfault_lte_event_handler);
}

#ifdef CONFIG_MEMFAULT_INFUSE_METRICS_NRF_MODEM

static void memfault_metrics_nrf_modem_collect_data(void)
{
	struct nrf_modem_network_state network;
	int tx_kbytes, rx_kbytes;

	nrf_modem_monitor_network_state(&network);

	MEMFAULT_METRIC_SET_UNSIGNED(ncs_lte_mode, network.lte_mode);
	MEMFAULT_METRIC_SET_UNSIGNED(ncs_lte_band, network.band);
	if (network.psm_cfg.active_time != -1) {
		MEMFAULT_METRIC_SET_SIGNED(ncs_lte_psm_tau_seconds, network.psm_cfg.tau);
		MEMFAULT_METRIC_SET_SIGNED(ncs_lte_psm_active_time_seconds,
					   network.psm_cfg.active_time);
	}
	if (network.edrx_cfg.edrx != -1.0f) {
		MEMFAULT_METRIC_SET_UNSIGNED(ncs_lte_edrx_interval_ms,
					     (int)(1000 * network.edrx_cfg.edrx));
		MEMFAULT_METRIC_SET_UNSIGNED(ncs_lte_edrx_ptw_ms,
					     (int)(1000 * network.edrx_cfg.ptw));
	}

	if (nrf_modem_monitor_connectivity_stats(&tx_kbytes, &rx_kbytes) == 0) {
		MEMFAULT_METRIC_SET_UNSIGNED(ncs_lte_tx_kilobytes, tx_kbytes);
		MEMFAULT_METRIC_SET_UNSIGNED(ncs_lte_rx_kilobytes, rx_kbytes);
	}
}

#endif /* CONFIG_MEMFAULT_INFUSE_METRICS_NRF_MODEM */

#endif /* CONFIG_MEMFAULT_INFUSE_NRF_MODEM */

void memfault_metrics_heartbeat_collect_data(void)
{
#ifdef CONFIG_MEMFAULT_INFUSE_METRICS_NRF_MODEM
	memfault_metrics_nrf_modem_collect_data();
#endif /* CONFIG_MEMFAULT_INFUSE_METRICS_NRF_MODEM */
}
