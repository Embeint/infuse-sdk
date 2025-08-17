/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/tasks/bt_scanner.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/tdf/definitions.h>
#include <infuse/time/epoch.h>

LOG_MODULE_REGISTER(task_bt_scanner, CONFIG_TASK_BT_SCANNER_LOG_LEVEL);

static struct task_bt_scanner_mem {
	struct tdf_infuse_bluetooth_rssi observed[CONFIG_TASK_RUNNER_TASK_BT_SCANNER_MAX_DEVICES];
	struct epacket_interface_cb interface_cb;
	const struct task_schedule *schedule;
	struct task_data *task;
	uint8_t max_observed;
	uint8_t num_observed;
} state;

static bool bt_received(struct net_buf *buf, bool decrypted, void *user_ctx)
{
	const struct task_bt_scanner_args *args = &state.schedule->task_args.infuse.bt_scanner;
	struct epacket_rx_metadata *meta = net_buf_user_data(buf);

	LOG_DBG("%llx: %012llx RSSI: %d dBm", meta->packet_device_id,
		sys_get_be48(meta->interface_address.bluetooth.a.val), meta->rssi);

	if (!decrypted && !(args->flags & TASK_BT_SCANNER_FLAGS_LOG_ENCRYPTED)) {
		return true;
	}
	if (state.max_observed && (state.num_observed >= state.max_observed)) {
		/* Cancellation is pending */
		return true;
	}

	struct tdf_infuse_bluetooth_rssi tdf_obs = {
		.infuse_id = meta->packet_device_id,
		.rssi = MAX(meta->rssi, INT8_MIN),
	};

	/* Have we already seen this device? */
	if (args->flags & TASK_BT_SCANNER_FLAGS_FILTER_DUPLICATES) {
		for (int i = 0; i < state.num_observed; i++) {
			if (state.observed[i].infuse_id == tdf_obs.infuse_id) {
				/* Update RSSI and exit */
				state.observed[i].rssi = tdf_obs.rssi;
				return true;
			}
		}
	}

	if (args->flags &
	    (TASK_BT_SCANNER_FLAGS_FILTER_DUPLICATES | TASK_BT_SCANNER_FLAGS_DEFER_LOGGING)) {
		/* Store the data for filtering/logging */
		memcpy(&state.observed[state.num_observed], &tdf_obs, sizeof(tdf_obs));
	}

	if (!(args->flags & TASK_BT_SCANNER_FLAGS_DEFER_LOGGING)) {
		/* Log the observation immediately */
		task_schedule_tdf_log(state.schedule, TASK_BT_SCANNER_LOG_INFUSE_BT,
				      TDF_INFUSE_BLUETOOTH_RSSI, sizeof(tdf_obs), epoch_time_now(),
				      &tdf_obs);
	}
	state.num_observed++;

	/* Have we reached the requested limit? */
	if (state.max_observed && (state.num_observed == state.max_observed)) {
		LOG_DBG("Limit reached (%d)", state.max_observed);
		/* Terminate the task early */
		task_workqueue_reschedule(state.task, K_NO_WAIT);
	}
	return true;
}

void task_bt_scanner_fn(struct k_work *work)
{
	const struct device *interface = DEVICE_DT_GET_ANY(embeint_epacket_bt_adv);
	struct task_data *task = task_data_from_work(work);
	const struct task_schedule *sch = task_schedule_from_data(task);
	const struct task_bt_scanner_args *args = &sch->task_args.infuse.bt_scanner;
	int rc;

	if (task->executor.workqueue.reschedule_counter > 0) {
		LOG_DBG("Terminating receive");
		if ((args->flags & TASK_BT_SCANNER_FLAGS_DEFER_LOGGING) && state.num_observed) {
			task_schedule_tdf_log_array(state.schedule, TASK_BT_SCANNER_LOG_INFUSE_BT,
						    TDF_INFUSE_BLUETOOTH_RSSI,
						    sizeof(state.observed[0]), state.num_observed,
						    epoch_time_now(), 0, state.observed);
		}
		epacket_unregister_callback(interface, &state.interface_cb);
		epacket_receive(interface, K_NO_WAIT);
		return;
	}

	state.schedule = sch;
	state.interface_cb.packet_received = bt_received;
	state.num_observed = 0;
	state.max_observed = args->max_logs;
	state.task = task;
	/* Limit max observed if state storage is required */
	if (args->flags &
	    (TASK_BT_SCANNER_FLAGS_FILTER_DUPLICATES | TASK_BT_SCANNER_FLAGS_DEFER_LOGGING)) {
		state.max_observed = args->max_logs == 0
					     ? ARRAY_SIZE(state.observed)
					     : MIN(args->max_logs, ARRAY_SIZE(state.observed));
	}

	epacket_register_callback(interface, &state.interface_cb);

	LOG_DBG("Starting receive");
	/* Start the receiving */
	rc = epacket_receive(interface, K_FOREVER);
	if (rc < 0) {
		LOG_ERR("Failed to start receive");
		return;
	}

	/* Wait until scanning finishes */
	if (args->duration_ms == 0) {
		/* A task that isn't pending is considered finished */
		task_workqueue_reschedule(task, K_HOURS(1));
	} else {
		task_workqueue_reschedule(task, K_MSEC(args->duration_ms));
	}
}
