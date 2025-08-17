/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <infuse/work_q.h>

static struct k_sem work_execute;

static void work_fn(struct k_work *work)
{
	k_sem_give(&work_execute);
}

static void expect_workq_delay(struct k_work_q *queue)
{
	struct k_work workqueue_tester;
	uint64_t start = k_uptime_get();
	uint64_t t_queued;
	uint64_t t_execution_delay;

	k_work_init(&workqueue_tester, work_fn);
	k_sem_init(&work_execute, 0, 1);

	zassert_not_null(queue);

	while (k_uptime_get() < (start + 10000)) {
		t_queued = k_uptime_get();
		k_work_submit(&workqueue_tester);
		zassert_equal(0, k_sem_take(&work_execute, K_SECONDS(2)));
		t_execution_delay = k_uptime_get() - t_queued;

		if (t_execution_delay > 100) {
			/* We saw a delay, test done */
			return;
		}

		/* Wait before testing again */
		k_sleep(K_MSEC(10));
	}
	zassert_unreachable();
}

ZTEST(workqueue_blocker, test_sysworkq)
{
	expect_workq_delay(&k_sys_work_q);
}

ZTEST(workqueue_blocker, test_tr_workq)
{
	expect_workq_delay(&infuse_iot_work_q);
}

ZTEST_SUITE(workqueue_blocker, NULL, NULL, NULL, NULL, NULL);
