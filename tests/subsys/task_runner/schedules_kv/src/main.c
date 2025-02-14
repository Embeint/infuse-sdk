/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <infuse/states.h>
#include <infuse/task_runner/runner.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

#define SCHEDULE_ID_BASE (sizeof(struct task_schedule) << 16)

ZTEST(task_runner_schedules_kv, test_schedules_kv_invalid_not_written)
{
	struct task_schedule schedules[2] = {
		{0},
		{
			.validity = TASK_VALID_ALWAYS,
			.periodicity_type = TASK_PERIODICITY_FIXED,
			.periodicity.fixed.period_s = 10,
		},
	};

	zassert_false(kv_store_key_exists(KV_KEY_TASK_SCHEDULES_DEFAULT_ID));
	zassert_false(kv_store_key_exists(KV_KEY_TASK_SCHEDULES + 0));
	zassert_false(kv_store_key_exists(KV_KEY_TASK_SCHEDULES + 1));

	task_runner_schedules_load(10, schedules, ARRAY_SIZE(schedules));

	zassert_true(kv_store_key_exists(KV_KEY_TASK_SCHEDULES_DEFAULT_ID));
	zassert_false(kv_store_key_exists(KV_KEY_TASK_SCHEDULES + 0));
	zassert_true(kv_store_key_exists(KV_KEY_TASK_SCHEDULES + 1));
}

ZTEST(task_runner_schedules_kv, test_schedules_kv_basic)
{
	struct task_schedule readback = {0};
	struct task_schedule schedule = {
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_FIXED,
		.periodicity.fixed.period_s = 10,
	};

	zassert_false(kv_store_key_exists(KV_KEY_TASK_SCHEDULES_DEFAULT_ID));
	zassert_false(kv_store_key_exists(KV_KEY_TASK_SCHEDULES + 0));

	/* Schedule written to KV store after load */
	task_runner_schedules_load(10, &schedule, 1);
	zassert_true(kv_store_key_exists(KV_KEY_TASK_SCHEDULES_DEFAULT_ID));
	zassert_true(kv_store_key_exists(KV_KEY_TASK_SCHEDULES + 0));

	/* Updated values without changing the schedule ID are reverted */
	schedule.periodicity.fixed.period_s = 15;
	task_runner_schedules_load(10, &schedule, 1);
	zassert_equal(10, schedule.periodicity.fixed.period_s);

	/* Updated values with a changed schedule ID are preserved */
	schedule.periodicity.fixed.period_s = 15;
	task_runner_schedules_load(11, &schedule, 1);
	zassert_equal(15, schedule.periodicity.fixed.period_s);

	/* Writing a value directly is preserved */
	schedule.periodicity.fixed.period_s = 20;
	zassert_equal(sizeof(schedule),
		      kv_store_write(KV_KEY_TASK_SCHEDULES + 0, &schedule, sizeof(schedule)));

	task_runner_schedules_load(11, &readback, 1);
	zassert_equal(20, readback.periodicity.fixed.period_s);
	zassert_mem_equal(&schedule, &readback, sizeof(readback));

	/* Locked schedules are not overwritten from KV store */
	schedule.validity |= TASK_LOCKED;
	schedule.periodicity.fixed.period_s = 9;

	task_runner_schedules_load(11, &schedule, 1);
	zassert_equal(TASK_LOCKED | TASK_VALID_ALWAYS, schedule.validity);
	zassert_equal(9, schedule.periodicity.fixed.period_s);
}

ZTEST(task_runner_schedules_kv, test_schedules_kv_load_many)
{
	struct task_schedule schedule = {
		.validity = TASK_VALID_ACTIVE,
		.periodicity_type = TASK_PERIODICITY_LOCKOUT,
		.periodicity.lockout.lockout_s = 60,
	};
	struct task_schedule schedule_null = {0};
	struct kv_task_schedules_default_id default_id = {SCHEDULE_ID_BASE | 10};

	/* Write 5 schedules to the KV store */
	zassert_equal(sizeof(default_id), kv_store_write(KV_KEY_TASK_SCHEDULES_DEFAULT_ID,
							 &default_id, sizeof(default_id)));
	for (int i = 0; i < 5; i++) {
		zassert_equal(sizeof(schedule), kv_store_write(KV_KEY_TASK_SCHEDULES + i, &schedule,
							       sizeof(schedule)));
	}

	struct task_schedule schedules[CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE] = {0};

	task_runner_schedules_load(10, schedules, CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE);

	for (int i = 0; i < 5; i++) {
		zassert_mem_equal(&schedule, &schedules[i], sizeof(schedule));
	}
	for (int i = 5; i < CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE; i++) {
		zassert_mem_equal(&schedule_null, &schedules[i], sizeof(schedule_null));
	}

	/* New schedule ID with fewer schedules should clear later values */
	task_runner_schedules_load(11, schedules, 3);
	for (int i = 0; i < 3; i++) {
		zassert_mem_equal(&schedule, &schedules[i], sizeof(schedule));
		zassert_true(kv_store_key_exists(KV_KEY_TASK_SCHEDULES + i));
	}
	for (int i = 3; i < CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE; i++) {
		zassert_false(kv_store_key_exists(KV_KEY_TASK_SCHEDULES + i));
	}

	task_runner_schedules_load(11, schedules, CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE);
	for (int i = 0; i < 3; i++) {
		zassert_mem_equal(&schedule, &schedules[i], sizeof(schedule));
	}
	for (int i = 3; i < CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE; i++) {
		zassert_mem_equal(&schedule_null, &schedules[i], sizeof(schedule_null));
	}
}

ZTEST(task_runner_schedules_kv, test_schedules_kv_load_too_many)
{
	struct task_schedule schedules[2 * CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE] = {0};
	struct task_schedule readback[2 * CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE] = {0};
	struct task_schedule schedule_null = {0};

	for (int i = 0; i < ARRAY_SIZE(schedules); i++) {
		schedules[i].validity = TASK_VALID_ACTIVE;
		schedules[i].periodicity_type = TASK_PERIODICITY_LOCKOUT;
		schedules[i].periodicity.lockout.lockout_s = 10;
	}

	/* Load with more default schedules than KV slots */
	task_runner_schedules_load(5, schedules, ARRAY_SIZE(schedules));

	/* Values should not be written past the end of enabled keys */
	for (int i = 0; i < CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE; i++) {
		zassert_true(kv_store_key_exists(KV_KEY_TASK_SCHEDULES + i));
	}
	for (int i = CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE; i < ARRAY_SIZE(schedules); i++) {
		zassert_false(kv_store_key_exists(KV_KEY_TASK_SCHEDULES + i));
	}

	/* Load with more default schedules than KV slots */
	task_runner_schedules_load(5, readback, ARRAY_SIZE(readback));

	for (int i = 0; i < CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE; i++) {
		zassert_mem_equal(&schedules[i], &readback[i], sizeof(schedules[0]));
	}
	for (int i = CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE; i < ARRAY_SIZE(readback); i++) {
		zassert_mem_equal(&schedule_null, &readback[i], sizeof(schedule_null));
	}
}

ZTEST(task_runner_schedules_kv, test_schedules_kv_load_corrupt)
{
	struct task_schedule schedules[5] = {0};
	struct task_schedule readback[5] = {0};
	struct task_schedule schedule_null = {0};

	/* Write 5 schedules to the KV store */
	for (int i = 0; i < 5; i++) {
		schedules[i].validity = TASK_VALID_ACTIVE;
		schedules[i].periodicity_type = TASK_PERIODICITY_LOCKOUT;
		schedules[i].periodicity.lockout.lockout_s = 50;
	}
	task_runner_schedules_load(20, schedules, ARRAY_SIZE(schedules));

	/* Intentionally corrupt stored schedule 3 */
	zassert_equal(10, kv_store_write(KV_KEY_TASK_SCHEDULES + 2, &schedules[2], 10));

	/* Load schedules again */
	task_runner_schedules_load(20, readback, ARRAY_SIZE(readback));
	for (int i = 0; i < ARRAY_SIZE(schedules); i++) {
		if (i == 2) {
			/* Schedule 3 should be zeroed out */
			zassert_mem_equal(&schedule_null, &readback[i], sizeof(schedule_null));
		} else {
			zassert_mem_equal(&schedules[i], &readback[i], sizeof(schedules[0]));
		}
	}
}

void test_init(void *fixture)
{
	kv_store_reset();
}

ZTEST_SUITE(task_runner_schedules_kv, NULL, NULL, test_init, NULL, NULL);
