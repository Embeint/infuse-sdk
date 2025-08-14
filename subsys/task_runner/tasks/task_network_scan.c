/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/logging/log.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/tasks/network_scan.h>
#include <infuse/tdf/definitions.h>
#include <infuse/time/epoch.h>

enum {
	PHASE_START = 0,
	PHASE_WIFI_SCAN_2G4_COMMON,
	PHASE_WIFI_SCAN_5G_COMMON,
	PHASE_LTE_START,
	PHASE_LTE_SCAN_NORMAL,
	PHASE_LTE_SCAN_GCI_HISTORY,
	PHASE_LTE_SCAN_GCI_EXTENDED,
	PHASE_DONE,
	PHASE_TIMEOUT = BIT(7),
};

#define WIFI_BSSID_MASK 0xFFFFFFFFFFF0ULL

TDF_LTE_TAC_CELLS_VAR(tdf_lte_tac_cells_n, CONFIG_TASK_RUNNER_TASK_NETWORK_SCAN_LTE_MAX_NEIGHBOURS);

static struct {
#ifdef CONFIG_LTE_LINK_CONTROL
	struct tdf_lte_tac_cells_n local_cells;
	struct tdf_lte_tac_cells global_cells[CONFIG_TASK_RUNNER_TASK_NETWORK_SCAN_LTE_MAX_GCI];
	uint8_t neighbour_cells;
	uint8_t gci_cells;
#endif /* CONFIG_LTE_LINK_CONTROL */
#ifdef CONFIG_WIFI
	struct tdf_wifi_ap_info wifi_aps[CONFIG_TASK_RUNNER_TASK_NETWORK_SCAN_WIFI_MAX_APS];
	struct net_mgmt_event_callback wifi_cb;
	uint8_t wifi_flags;
	uint8_t aps_found;
	bool manual_if_up;
#endif /* CONFIG_WIFI */
	struct task_data *running;
	bool registered;
	uint8_t phase;
} state;

LOG_MODULE_REGISTER(task_network_scan, CONFIG_TASK_NETWORK_SCAN_LOG_LEVEL);

#ifdef CONFIG_WIFI

static void scan_result_handle(const struct wifi_scan_result *entry)
{
	uint64_t bssid, bssid_masked, temp;
	struct tdf_wifi_ap_info *info;

	if (state.aps_found >= ARRAY_SIZE(state.wifi_aps)) {
		/* Already logged maximum APs */
		return;
	}
	if (entry->mac_length != 6) {
		/* Reporting a network without a valid BSSID doesn't make sense */
		LOG_DBG("Skipping network without BSSID: '%s'",
			entry->ssid_length ? (char *)entry->ssid : "");
		return;
	}

	bssid = sys_get_be48(entry->mac);
	bssid_masked = bssid & WIFI_BSSID_MASK;

	/* Filter out locally administered MACs */
	if (!(state.wifi_flags & TASK_NETWORK_SCAN_WIFI_FLAGS_INCLUDE_LOCALLY_ADMINISTERED)) {
		if (entry->mac[0] & 0x02) {
			LOG_DBG("%s %012llX: '%s'", "Locally Administered", bssid, entry->ssid);
			return;
		}
	}

	/* Filter out duplicate BSSIDs */
	if (!(state.wifi_flags & TASK_NETWORK_SCAN_WIFI_FLAGS_INCLUDE_DUPLICATES)) {
		temp = sys_get_be48(state.wifi_aps->bssid.val) & WIFI_BSSID_MASK;

		for (int i = 0; i < state.aps_found; i++) {
			if (bssid_masked == temp) {
				LOG_DBG("%s %012llX: '%s'", "Duplicate BSSID", bssid, entry->ssid);
				return;
			}
		}
	}

	info = &state.wifi_aps[state.aps_found];

	/* Store AP information */
	LOG_INF("%s %012llX '%s' (%d dBm)", "BSSID", bssid, entry->ssid, entry->rssi);
	memcpy(info->bssid.val, entry->mac, 6);
	info->channel = entry->channel;
	info->rsrp = entry->rssi;
	state.aps_found++;
}

static void scan_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event,
			       struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_WIFI_SCAN_RESULT:
		scan_result_handle(cb->info);
		break;
	case NET_EVENT_WIFI_SCAN_DONE:
		/* Clear timeout state */
		state.phase &= ~PHASE_TIMEOUT;
		/* Reschedule workqueue item to process scan results */
		task_workqueue_reschedule(state.running, K_MSEC(10));
		break;
	default:
		break;
	}
}

static int wifi_scan_handle(const struct task_network_scan_args *args)
{
	struct wifi_scan_params params = {0};
	struct net_if *iface;
	int rc = 0;

	iface = net_if_get_first_wifi();

	/* Have we already found enough APs? */
	if (state.aps_found >= args->wifi.desired_aps) {
		rc = 1;
		goto wifi_done;
	}

	params.scan_type = args->wifi.flags & TASK_NETWORK_SCAN_WIFI_FLAGS_SCAN_ACTIVE
				   ? WIFI_SCAN_TYPE_ACTIVE
				   : WIFI_SCAN_TYPE_PASSIVE;
	/* Don't configure this value since we may discard multiple networks in the callback */
	params.max_bss_cnt = 0;

	/* Initiate the next phase of the search */
	switch (state.phase) {
	case PHASE_START:
		/* Enable interface if not yet up */
		if (!net_if_is_admin_up(iface)) {
			rc = net_if_up(iface);
			if (rc < 0) {
				goto wifi_done;
			}
			state.manual_if_up = true;
		}
		state.wifi_flags = args->wifi.flags;

		if (args->wifi.flags & TASK_NETWORK_SCAN_WIFI_FLAGS_SCAN_PROGRESSIVE) {
			state.phase = PHASE_WIFI_SCAN_2G4_COMMON;
			/* Most common 2.4 GHz bands */
			params.bands = BIT(WIFI_FREQ_BAND_2_4_GHZ);
			params.band_chan[0] = (struct wifi_band_channel){WIFI_FREQ_BAND_2_4_GHZ, 1};
			params.band_chan[1] = (struct wifi_band_channel){WIFI_FREQ_BAND_2_4_GHZ, 6};
			params.band_chan[2] =
				(struct wifi_band_channel){WIFI_FREQ_BAND_2_4_GHZ, 11};
		} else {
			/* Scan all bands */
			state.phase = PHASE_WIFI_SCAN_5G_COMMON;
			params.bands = BIT(WIFI_FREQ_BAND_2_4_GHZ) | BIT(WIFI_FREQ_BAND_5_GHZ);
		}
		break;
	case PHASE_WIFI_SCAN_2G4_COMMON:
		state.phase = PHASE_WIFI_SCAN_5G_COMMON;
		/* Most common 5 GHz bands */
		params.bands = BIT(WIFI_FREQ_BAND_5_GHZ);
		params.band_chan[0] = (struct wifi_band_channel){WIFI_FREQ_BAND_5_GHZ, 36};
		params.band_chan[1] = (struct wifi_band_channel){WIFI_FREQ_BAND_5_GHZ, 40};
		params.band_chan[2] = (struct wifi_band_channel){WIFI_FREQ_BAND_5_GHZ, 44};
		params.band_chan[3] = (struct wifi_band_channel){WIFI_FREQ_BAND_5_GHZ, 48};
		params.band_chan[4] = (struct wifi_band_channel){WIFI_FREQ_BAND_5_GHZ, 149};
		params.band_chan[5] = (struct wifi_band_channel){WIFI_FREQ_BAND_5_GHZ, 153};
		params.band_chan[6] = (struct wifi_band_channel){WIFI_FREQ_BAND_5_GHZ, 157};
		params.band_chan[7] = (struct wifi_band_channel){WIFI_FREQ_BAND_5_GHZ, 161};
		break;
	case PHASE_WIFI_SCAN_5G_COMMON:
		goto wifi_done;
	}

	rc = net_mgmt(NET_REQUEST_WIFI_SCAN, iface, &params, sizeof(struct wifi_scan_params));
	LOG_INF("Requesting Wi-Fi AP scan (%d)", rc);
	return rc;

wifi_done:
	state.phase = PHASE_LTE_START;
	if (state.manual_if_up) {
		/* Disable interface if it we enabled it */
		(void)net_if_down(iface);
	}
	return rc;
}

#endif /* CONFIG_WIFI */

#ifdef CONFIG_LTE_LINK_CONTROL

#include <modem/lte_lc.h>
#include <modem/modem_info.h>

#define TDF_LTE_RSRP(modem_rsrp)                                                                   \
	((modem_rsrp) == LTE_LC_CELL_RSRP_INVALID ? 255 : -RSRP_IDX_TO_DBM(modem_rsrp))

#define TDF_LTE_RSRQ(modem_rsrq)                                                                   \
	((modem_rsrq) == LTE_LC_CELL_RSRQ_INVALID ? -128 : RSRQ_IDX_TO_DB(modem_rsrq))

void network_scan_lte_handler(const struct lte_lc_evt *const evt)
{
	struct tdf_lte_tac_cells_n *lc = &state.local_cells;
	struct tdf_lte_tac_cells *gc = state.global_cells;
	const struct lte_lc_cells_info *info;

	if (evt->type != LTE_LC_EVT_NEIGHBOR_CELL_MEAS) {
		return;
	}
	info = &evt->cells_info;

	/* Clear timeout state */
	state.phase &= ~PHASE_TIMEOUT;

	if (state.phase == PHASE_LTE_SCAN_NORMAL) {
		LOG_INF("Serving Cell Valid: %s, Neighbour Cells: %d",
			info->current_cell.id == LTE_LC_CELL_EUTRAN_ID_INVALID ? "No" : "Yes",
			info->ncells_count);
		/* Serving Cell information */
		lc->cell.mcc = info->current_cell.mcc;
		lc->cell.mnc = info->current_cell.mnc;
		lc->cell.eci = info->current_cell.id;
		lc->cell.tac = info->current_cell.tac;
		lc->earfcn = info->current_cell.earfcn;
		lc->rsrp = TDF_LTE_RSRP(info->current_cell.rsrp);
		lc->rsrq = TDF_LTE_RSRQ(info->current_cell.rsrq);

		/* Neighbour cells */
		state.neighbour_cells =
			MIN(info->ncells_count, ARRAY_SIZE(state.local_cells.neighbours));
		for (int i = 0; i < state.neighbour_cells; i++) {
			lc->neighbours[i].earfcn = info->neighbor_cells[i].earfcn;
			lc->neighbours[i].pci = info->neighbor_cells[i].phys_cell_id;
			lc->neighbours[i].time_diff = info->neighbor_cells[i].time_diff;
			lc->neighbours[i].rsrp = TDF_LTE_RSRP(info->neighbor_cells[i].rsrp);
			lc->neighbours[i].rsrq = TDF_LTE_RSRQ(info->neighbor_cells[i].rsrq);
		}
	} else {
		LOG_INF("Global Cells: %d", info->gci_cells_count);
		state.gci_cells = 0;

		/* Global cells */
		for (int i = 0; i < info->gci_cells_count; i++) {
			/* Skip cell if it matches the serving cell */
			if ((info->gci_cells[i].id == lc->cell.eci) &&
			    (info->gci_cells[i].tac == lc->cell.tac)) {
				LOG_DBG("GCI cell %d matches serving cell", i);
				continue;
			}
			gc[state.gci_cells].cell.mcc = info->gci_cells[i].mcc;
			gc[state.gci_cells].cell.mnc = info->gci_cells[i].mnc;
			gc[state.gci_cells].cell.eci = info->gci_cells[i].id;
			gc[state.gci_cells].cell.tac = info->gci_cells[i].tac;
			gc[state.gci_cells].earfcn = info->gci_cells[i].earfcn;
			gc[state.gci_cells].rsrp = TDF_LTE_RSRP(info->gci_cells[i].rsrp);
			gc[state.gci_cells].rsrq = TDF_LTE_RSRQ(info->gci_cells[i].rsrq);
			state.gci_cells += 1;
			if (state.gci_cells >= ARRAY_SIZE(state.global_cells)) {
				break;
			}
		}
	}

	/* Reschedule workqueue item to process scan results */
	task_workqueue_reschedule(state.running, K_NO_WAIT);
}

static int lte_scan_handle(const struct task_network_scan_args *args)
{
	struct lte_lc_ncellmeas_params ncellmeas_params;
	int cells_found;
	int rc = 0;

	/* Have we already found enough cells? */
	cells_found = ((state.local_cells.cell.eci == LTE_LC_CELL_EUTRAN_ID_INVALID) ? 0 : 1) +
		      state.neighbour_cells + state.gci_cells;
	if (cells_found >= args->lte.desired_cells) {
		return 1;
	}

	/* Initiate the next phase of the search */
	switch (state.phase) {
	case PHASE_START:
	case PHASE_LTE_START:
		ncellmeas_params.search_type = LTE_LC_NEIGHBOR_SEARCH_TYPE_EXTENDED_LIGHT;
		ncellmeas_params.gci_count = 0;
		rc = lte_lc_neighbor_cell_measurement(&ncellmeas_params);
		state.phase = PHASE_LTE_SCAN_NORMAL;
		break;
	case PHASE_LTE_SCAN_NORMAL:
		ncellmeas_params.search_type = LTE_LC_NEIGHBOR_SEARCH_TYPE_GCI_DEFAULT;
		ncellmeas_params.gci_count = args->lte.desired_cells - state.neighbour_cells;
		rc = lte_lc_neighbor_cell_measurement(&ncellmeas_params);
		state.phase = PHASE_LTE_SCAN_GCI_HISTORY;
		break;
	case PHASE_LTE_SCAN_GCI_HISTORY:
		ncellmeas_params.search_type = LTE_LC_NEIGHBOR_SEARCH_TYPE_GCI_EXTENDED_LIGHT;
		ncellmeas_params.gci_count = args->lte.desired_cells - state.neighbour_cells;
		rc = lte_lc_neighbor_cell_measurement(&ncellmeas_params);
		state.phase = PHASE_LTE_SCAN_GCI_EXTENDED;
		break;
	case PHASE_LTE_SCAN_GCI_EXTENDED:
		rc = 1;
		break;
	default:
		LOG_DBG("Unexpected state (%d)", state.phase);
		rc = -EINVAL;
	}
	return rc;
}
#endif /* CONFIG_LTE_LINK_CONTROL */

void network_scan_task_fn(struct k_work *work)
{
	struct task_data *task = task_data_from_work(work);
	const struct task_schedule *sch = task_schedule_from_data(task);
	const struct task_network_scan_args *args = &sch->task_args.infuse.network_scan;
	uint64_t epoch_time;
	size_t len;
	int rc = 0;

	state.running = task;

	if (task->executor.workqueue.reschedule_counter == 0) {
		state.phase = PHASE_START;
#ifdef CONFIG_LTE_LINK_CONTROL
		state.local_cells.cell.eci = LTE_LC_CELL_EUTRAN_ID_INVALID;
		state.neighbour_cells = 0;
		state.gci_cells = 0;
		if (!state.registered) {
			lte_lc_register_handler(network_scan_lte_handler);
		}
#endif /* CONFIG_LTE_LINK_CONTROL */
#ifdef CONFIG_WIFI
		if (!state.registered) {
			net_mgmt_init_event_callback(&state.wifi_cb, scan_event_handler,
						     NET_EVENT_WIFI_SCAN_RESULT |
							     NET_EVENT_WIFI_SCAN_DONE);
			net_mgmt_add_event_callback(&state.wifi_cb);
		}
		state.aps_found = 0;
#endif /* CONFIG_WIFI */
		state.registered = true;
	}

	if ((task_runner_task_block(&task->terminate_signal, K_NO_WAIT) == 1) ||
	    (state.phase & PHASE_TIMEOUT)) {
#ifdef CONFIG_LTE_LINK_CONTROL
		/* Early wake by runner to terminate, or callback timed out */
		(void)lte_lc_neighbor_cell_measurement_cancel();
#endif /* CONFIG_LTE_LINK_CONTROL */
#ifdef CONFIG_WIFI
		/* No way to request cancelling an ongoing scan */
#endif /* CONFIG_WIFI */
		return;
	}
	LOG_DBG("Task phase: %d", state.phase);

	if (state.phase < PHASE_LTE_START) {
#ifdef CONFIG_WIFI
		if (args->flags & TASK_NETWORK_SCAN_FLAGS_WIFI_CELLS) {
			/* Wi-Fi scanning */
			rc = wifi_scan_handle(args);
			if ((args->flags & TASK_NETWORK_SCAN_FLAGS_SKIP_LTE_IF_WIFI_GOOD) &&
			    (rc == 1) && (state.aps_found >= args->wifi.desired_aps)) {
				/* Skip the LTE phase */
				LOG_INF("Wi-Fi found %d/%d APs, skipping LTE", state.aps_found,
					args->wifi.desired_aps);
				state.phase = PHASE_DONE;
			}

		} else {
			/* Wi-Fi not requested, proceed to LTE */
			state.phase = PHASE_LTE_START;
		}
#else
		/* Wi-Fi not available, skip to LTE */
		state.phase = PHASE_LTE_START;
#endif /* CONFIG_WIFI */
	}

	if ((state.phase >= PHASE_LTE_START) && (state.phase < PHASE_DONE)) {
#ifdef CONFIG_LTE_LINK_CONTROL
		if (args->flags & TASK_NETWORK_SCAN_FLAGS_LTE_CELLS) {
			rc = lte_scan_handle(args);
		} else {
			state.phase = PHASE_DONE;
			rc = 1;
		}
#else
		/* LTE not available, done */
		state.phase = PHASE_DONE;
		rc = 1;
#endif /* CONFIG_WIFI */
	}

	if (rc == 0) {
		/* Next phase scheduled */
		state.phase |= PHASE_TIMEOUT;
		task_workqueue_reschedule(task, K_MINUTES(1));
		return;
	}
	if (rc < 0) {
		LOG_WRN("Failed to start next step for state %d (%d)", state.phase, rc);
	}

	epoch_time = epoch_time_now();

	struct tdf_network_scan_count count = {
#ifdef CONFIG_WIFI
		.num_wifi = state.aps_found,
#endif /* CONFIG_WIFI */
#ifdef CONFIG_LTE_LINK_CONTROL
		.num_lte = state.gci_cells +
			   (state.local_cells.cell.eci != LTE_LC_CELL_EUTRAN_ID_INVALID),
#endif /* CONFIG_LTE_LINK_CONTROL */
	};

	/* Network scan count */
	TASK_SCHEDULE_TDF_LOG(sch, TASK_NETWORK_SCAN_LOG_COUNT, TDF_NETWORK_SCAN_COUNT, epoch_time,
			      &count);

#ifdef CONFIG_WIFI
	if (state.aps_found > 0) {
		/* Individual APs in a TDF_ARRAY_TIME */
		TASK_SCHEDULE_TDF_LOG_ARRAY(sch, TASK_NETWORK_SCAN_LOG_WIFI_AP, TDF_WIFI_AP_INFO,
					    state.aps_found, epoch_time, 0, state.wifi_aps);
	}
#endif /* CONFIG_WIFI */
#ifdef CONFIG_LTE_LINK_CONTROL
	if (state.local_cells.cell.eci != LTE_LC_CELL_EUTRAN_ID_INVALID) {
		len = sizeof(struct tdf_lte_tac_cells) +
		      (state.neighbour_cells * sizeof(struct tdf_struct_lte_cell_neighbour));

		/* TAC info with a trailing neighbour array */
		task_schedule_tdf_log(sch, TASK_NETWORK_SCAN_LOG_LTE_CELLS, TDF_LTE_TAC_CELLS, len,
				      epoch_time, &state.local_cells);
	}
	if (state.gci_cells > 0) {
		/* Individual cells in a TDF_ARRAY_TIME */
		TASK_SCHEDULE_TDF_LOG_ARRAY(sch, TASK_NETWORK_SCAN_LOG_LTE_CELLS, TDF_LTE_TAC_CELLS,
					    state.gci_cells, epoch_time, 0, state.global_cells);
	}
#endif /* CONFIG_LTE_LINK_CONTROL */
}
