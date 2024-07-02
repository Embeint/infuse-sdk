/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/kernel.h>

#include <infuse/time/civil.h>
#include <infuse/task_runner/runner.h>

static struct k_work_delayable iterate_work;
extern struct k_work_q task_runner_workq;

static void iterate_worker(struct k_work *work)
{
	uint32_t gps_time = civil_time_seconds(civil_time_now());
	uint32_t next_iter = k_uptime_seconds() + 1;

	/* Iterate the runner */
	task_runner_iterate(k_uptime_seconds(), gps_time, 100);

	/* Schedule the next iteration */
	k_work_schedule_for_queue(&task_runner_workq, &iterate_work,
				  K_TIMEOUT_ABS_MS(next_iter * MSEC_PER_SEC));
}

void task_runner_start_auto_iterate(void)
{
	/* Initialise auto iterate worker and start */
	k_work_init_delayable(&iterate_work, iterate_worker);
	k_work_schedule_for_queue(&task_runner_workq, &iterate_work, K_NO_WAIT);
}
