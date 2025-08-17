/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/random/random.h>
#include <zephyr/logging/log.h>

#include <infuse/work_q.h>

static struct k_work_delayable sys_workq_blocker;
static struct k_work_delayable tr_workq_blocker;

LOG_MODULE_REGISTER(workqueue_blocker, LOG_LEVEL_INF);

static uint32_t generate_random_delay(uint32_t min, uint32_t max)
{
	return min + sys_rand32_get() % (max - min);
}

static void blocker(struct k_work *work)
{
	struct k_work_delayable *delayable = k_work_delayable_from_work(work);
	uint32_t delay = generate_random_delay(CONFIG_WORKQUEUE_BLOCK_DURATION_MIN_MS,
					       CONFIG_WORKQUEUE_BLOCK_DURATION_MAX_MS);

	/* Sleep for somewhere between 500 and 1000ms */
	LOG_WRN("Blocking %s for %d ms", _current->name, delay);
	k_sleep(K_MSEC(delay));

	/* Reschedule again */
	delay = generate_random_delay(CONFIG_WORKQUEUE_BLOCK_PERIODICITY_MIN_MS,
				      CONFIG_WORKQUEUE_BLOCK_PERIODICITY_MAX_MS);
	if (delayable == &sys_workq_blocker) {
		k_work_reschedule(&sys_workq_blocker, K_MSEC(delay));
	} else {
		infuse_work_reschedule(&tr_workq_blocker, K_MSEC(delay));
	}
}

static int workqueue_blocker_init(void)
{
	uint32_t delay = generate_random_delay(CONFIG_WORKQUEUE_BLOCK_PERIODICITY_MIN_MS,
					       CONFIG_WORKQUEUE_BLOCK_PERIODICITY_MAX_MS);

	k_work_init_delayable(&sys_workq_blocker, blocker);
	k_work_init_delayable(&tr_workq_blocker, blocker);

	/* Initial delay */
	k_work_reschedule(&sys_workq_blocker, K_MSEC(delay));
	infuse_work_reschedule(&tr_workq_blocker, K_MSEC(delay));
	return 0;
}

SYS_INIT(workqueue_blocker_init, APPLICATION, 99);
