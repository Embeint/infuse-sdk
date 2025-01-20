/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/logging/log.h>
#include <zephyr/pm/device_runtime.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/tasks/network_scan.h>
#include <infuse/tdf/definitions.h>
#include <infuse/time/epoch.h>

#include <modem/lte_lc.h>
#include <modem/modem_info.h>

enum {
	PHASE_IDLE = 0,
	PHASE_LTE_SCAN_NORMAL,
	PHASE_LTE_SCAN_GCI_HISTORY,
	PHASE_LTE_SCAN_GCI_EXTENDED,
	PHASE_TIMEOUT = BIT(7),
};

TDF_LTE_TAC_CELLS_VAR(tdf_lte_tac_cells_8, 8);

static struct {
	struct tdf_lte_tac_cells_8 local_cells;
	struct tdf_lte_tac_cells global_cells[8];
	struct task_data *running;
	bool registered;
	uint8_t neighbour_cells;
	uint8_t gci_cells;
	uint8_t phase;
} state;

#define TDF_LTE_RSRP(modem_rsrp)                                                                   \
	((modem_rsrp) == LTE_LC_CELL_RSRP_INVALID ? 255 : -RSRP_IDX_TO_DBM(modem_rsrp))

#define TDF_LTE_RSRQ(modem_rsrq)                                                                   \
	((modem_rsrq) == LTE_LC_CELL_RSRQ_INVALID ? -128 : RSRQ_IDX_TO_DB(modem_rsrq))

LOG_MODULE_REGISTER(task_network_scan, CONFIG_TASK_NETWORK_SCAN_LOG_LEVEL);

void network_scan_lte_handler(const struct lte_lc_evt *const evt)
{
	struct tdf_lte_tac_cells_8 *lc = &state.local_cells;
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

void network_scan_task_fn(struct k_work *work)
{
	struct task_data *task = task_data_from_work(work);
	const struct task_schedule *sch = task_schedule_from_data(task);
	const struct task_network_scan_args *args = &sch->task_args.infuse.network_scan;
	struct lte_lc_ncellmeas_params ncellmeas_params;
	uint8_t cells_found;
	uint64_t epoch_time;
	size_t len;
	int rc = 0;

	state.running = task;

	if (!state.registered) {
		lte_lc_register_handler(network_scan_lte_handler);
		state.registered = true;
	}

	if (task->executor.workqueue.reschedule_counter == 0) {
		state.phase = PHASE_IDLE;
		state.local_cells.cell.eci = LTE_LC_CELL_EUTRAN_ID_INVALID;
		state.neighbour_cells = 0;
		state.gci_cells = 0;
	}

	if ((task_runner_task_block(&task->terminate_signal, K_NO_WAIT) == 1) ||
	    (state.phase & PHASE_TIMEOUT)) {
		/* Early wake by runner to terminate, or callback timed out */
		(void)lte_lc_neighbor_cell_measurement_cancel();
		return;
	}
	LOG_DBG("Task phase: %d", state.phase);

	/* Have we already found enough cells? */
	cells_found = ((state.local_cells.cell.eci == LTE_LC_CELL_EUTRAN_ID_INVALID) ? 0 : 1) +
		      state.neighbour_cells + state.gci_cells;
	if (cells_found >= args->lte.desired_cells) {
		goto log_outputs;
	}

	/* Initiate the next phase of the search */
	switch (state.phase) {
	case PHASE_IDLE:
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
		goto log_outputs;
	default:
		LOG_DBG("Unexpected state (%d)", state.phase);
		rc = -EINVAL;
	}
	if (rc) {
		LOG_WRN("Failed to start next step for state %d", state.phase);
		goto log_outputs;
	}

	state.phase |= PHASE_TIMEOUT;
	task_workqueue_reschedule(task, K_MINUTES(1));
	return;
log_outputs:

	epoch_time = epoch_time_now();

	if (state.local_cells.cell.eci != LTE_LC_CELL_EUTRAN_ID_INVALID) {
		len = sizeof(struct tdf_lte_tac_cells) +
		      (state.neighbour_cells * sizeof(struct tdf_struct_lte_cell_neighbour));

		/* TAC info with a trailing neighbour array */
		task_schedule_tdf_log(sch, TASK_NETWORK_SCAN_LOG_LTE_CELLS, TDF_LTE_TAC_CELLS, len,
				      epoch_time, &state.local_cells);
	}
	if (state.gci_cells > 0) {
		/* Individual cells in a TDF_ARRAY_TIME */
		task_schedule_tdf_log_array(sch, TASK_NETWORK_SCAN_LOG_LTE_CELLS, TDF_LTE_TAC_CELLS,
					    sizeof(struct tdf_lte_tac_cells), state.gci_cells,
					    epoch_time, 0, state.global_cells);
	}
}
