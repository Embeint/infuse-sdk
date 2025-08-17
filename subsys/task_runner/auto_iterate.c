/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/work_q.h>
#include <infuse/time/epoch.h>
#include <infuse/task_runner/runner.h>
#include <infuse/tdf/definitions.h>
#include <infuse/zbus/channels.h>
#include <infuse/states.h>

static struct k_work_delayable iterate_work;

INFUSE_ZBUS_CHAN_DECLARE(INFUSE_ZBUS_CHAN_BATTERY);

#define CHAN_BAT INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY)

static void iterate_worker(struct k_work *work)
{
	INFUSE_STATES_ARRAY(states);
	int64_t uptime_ticks = k_uptime_ticks();
	uint32_t gps_time = epoch_time_seconds(epoch_time_from_ticks(uptime_ticks));
	uint32_t uptime_sec = k_ticks_to_sec_floor32(uptime_ticks);
	/* Default charge is 0% until measured */
	uint8_t charge = 0;

	/* Get battery charge from zbus */
	INFUSE_ZBUS_TYPE(INFUSE_ZBUS_CHAN_BATTERY) battery;

	if (zbus_chan_pub_stats_count(CHAN_BAT) > 0) {
		zbus_chan_read(CHAN_BAT, &battery, K_FOREVER);
		charge = battery.soc;
	}

	/* Iterate the runner */
	infuse_states_snapshot(states);
	task_runner_iterate(states, uptime_sec, gps_time, charge);
	infuse_states_tick(states);

	/* Schedule the next iteration */
	infuse_work_schedule(&iterate_work, K_TIMEOUT_ABS_SEC(uptime_sec + 1));
}

struct k_work_delayable *task_runner_start_auto_iterate(void)
{
	/* Initialise auto iterate worker and start */
	k_work_init_delayable(&iterate_work, iterate_worker);
	infuse_work_schedule(&iterate_work, K_NO_WAIT);
	return &iterate_work;
}
