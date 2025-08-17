/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <infuse/states.h>
#include <infuse/task_runner/runner.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

/* Declare the internal function */
int task_runner_schedules_load(
	uint16_t schedules_id, const struct task_schedule *default_schedules,
	uint8_t num_default_schedules,
	struct task_schedule out_schedules[CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE]);

struct task_schedule out_schedules[CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE] = {0};

ZTEST(task_runner_schedules_kv, test_schedules_kv_states_define)
{
	struct task_schedule schedules1[2];
	struct task_schedule schedules2[15];
	struct task_schedule schedules3[63];

	TASK_SCHEDULE_STATES_DEFINE(test_states1, schedules1);
	TASK_SCHEDULE_STATES_DEFINE(test_states2, schedules2);
	TASK_SCHEDULE_STATES_DEFINE(test_states3, schedules3);

	(void)schedules1;
	(void)schedules2;
	(void)schedules3;

	/* Always created with CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE entries */
	zassert_equal(CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE, ARRAY_SIZE(test_states1));
	zassert_equal(CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE, ARRAY_SIZE(test_states2));
	zassert_equal(CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE, ARRAY_SIZE(test_states3));
}

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
	int num_eval;

	zassert_false(kv_store_key_exists(KV_KEY_TASK_SCHEDULES_DEFAULT_ID));
	zassert_false(kv_store_key_exists(KV_KEY_TASK_SCHEDULES + 0));
	zassert_false(kv_store_key_exists(KV_KEY_TASK_SCHEDULES + 1));

	num_eval = task_runner_schedules_load(10, schedules, ARRAY_SIZE(schedules), out_schedules);
	zassert_equal(2, num_eval);

	zassert_true(kv_store_key_exists(KV_KEY_TASK_SCHEDULES_DEFAULT_ID));
	zassert_false(kv_store_key_exists(KV_KEY_TASK_SCHEDULES + 0));
	zassert_true(kv_store_key_exists(KV_KEY_TASK_SCHEDULES + 1));
}

ZTEST(task_runner_schedules_kv, test_schedules_kv_basic)
{
	struct task_schedule schedule = {
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_FIXED,
		.periodicity.fixed.period_s = 10,
	};
	int num_eval;

	zassert_false(kv_store_key_exists(KV_KEY_TASK_SCHEDULES_DEFAULT_ID));
	zassert_false(kv_store_key_exists(KV_KEY_TASK_SCHEDULES + 0));

	/* Schedule written to KV store after load */
	num_eval = task_runner_schedules_load(10, &schedule, 1, out_schedules);
	zassert_equal(1, num_eval);
	zassert_true(kv_store_key_exists(KV_KEY_TASK_SCHEDULES_DEFAULT_ID));
	zassert_true(kv_store_key_exists(KV_KEY_TASK_SCHEDULES + 0));

	/* Updated values without changing the schedule ID are reverted */
	schedule.periodicity.fixed.period_s = 15;
	num_eval = task_runner_schedules_load(10, &schedule, 1, out_schedules);
	zassert_equal(1, num_eval);
	zassert_equal(10, out_schedules[0].periodicity.fixed.period_s);

	/* Updated values with a changed schedule ID are preserved */
	schedule.periodicity.fixed.period_s = 15;
	num_eval = task_runner_schedules_load(11, &schedule, 1, out_schedules);
	zassert_equal(1, num_eval);
	zassert_equal(15, out_schedules[0].periodicity.fixed.period_s);

	/* Writing a value directly is preserved */
	schedule.periodicity.fixed.period_s = 20;
	zassert_equal(
		sizeof(struct task_schedule),
		kv_store_write(KV_KEY_TASK_SCHEDULES + 0, &schedule, sizeof(struct task_schedule)));

	num_eval = task_runner_schedules_load(11, &schedule, 1, out_schedules);
	zassert_equal(1, num_eval);
	zassert_equal(20, out_schedules[0].periodicity.fixed.period_s);
	zassert_mem_equal(&schedule, &out_schedules[0], sizeof(struct task_schedule));

	/* Locked schedules are not overwritten from KV store */
	schedule.validity |= TASK_LOCKED;
	schedule.periodicity.fixed.period_s = 9;

	num_eval = task_runner_schedules_load(11, &schedule, 1, out_schedules);
	zassert_equal(1, num_eval);
	zassert_equal(TASK_LOCKED | TASK_VALID_ALWAYS, schedule.validity);
	zassert_equal(9, out_schedules[0].periodicity.fixed.period_s);
}

ZTEST(task_runner_schedules_kv, test_schedules_kv_load_many)
{
	struct task_schedule schedules[5] = {0};
	struct task_schedule schedule_null = {0};
	int num_eval;

	for (int i = 0; i < ARRAY_SIZE(schedules); i++) {
		schedules[i].validity = TASK_VALID_ACTIVE;
		schedules[i].periodicity_type = TASK_PERIODICITY_LOCKOUT;
		schedules[i].periodicity.lockout.lockout_s = 3 + i;
	}

	/* Write 5 schedules to the KV store */
	num_eval = task_runner_schedules_load(10, schedules, ARRAY_SIZE(schedules), out_schedules);
	zassert_equal(5, num_eval);

	for (int i = 0; i < ARRAY_SIZE(schedules); i++) {
		zassert_mem_equal(&schedules[i], &out_schedules[i], sizeof(struct task_schedule));
	}
	for (int i = ARRAY_SIZE(schedules); i < CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE; i++) {
		zassert_mem_equal(&schedule_null, &out_schedules[i], sizeof(struct task_schedule));
	}

	/* New schedule ID with fewer schedules should clear later values */
	num_eval = task_runner_schedules_load(11, schedules, 3, out_schedules);
	zassert_equal(3, num_eval);
	for (int i = 0; i < 3; i++) {
		zassert_mem_equal(&schedules[i], &out_schedules[i], sizeof(struct task_schedule));
		zassert_true(kv_store_key_exists(KV_KEY_TASK_SCHEDULES + i));
	}
	for (int i = 3; i < CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE; i++) {
		zassert_false(kv_store_key_exists(KV_KEY_TASK_SCHEDULES + i));
	}

	/* Providing more values now doesn't change anything without an ID change */
	num_eval = task_runner_schedules_load(11, schedules, ARRAY_SIZE(schedules), out_schedules);
	zassert_equal(3, num_eval);
	for (int i = 0; i < 3; i++) {
		zassert_mem_equal(&schedules[i], &out_schedules[i], sizeof(struct task_schedule));
	}
	for (int i = 3; i < CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE; i++) {
		zassert_mem_equal(&schedule_null, &out_schedules[i], sizeof(struct task_schedule));
	}
}

ZTEST(task_runner_schedules_kv, test_schedules_kv_load_too_many)
{
	struct task_schedule schedules[2 * CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE] = {0};
	int num_eval;

	for (int i = 0; i < ARRAY_SIZE(schedules); i++) {
		schedules[i].validity = TASK_VALID_ACTIVE;
		schedules[i].periodicity_type = TASK_PERIODICITY_LOCKOUT;
		schedules[i].periodicity.lockout.lockout_s = 10;
	}

	/* Load with more default schedules than KV slots */
	num_eval = task_runner_schedules_load(5, schedules, ARRAY_SIZE(schedules), out_schedules);
	zassert_equal(CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE, num_eval);

	/* Values should not be written past the end of enabled keys */
	for (int i = 0; i < CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE; i++) {
		zassert_true(kv_store_key_exists(KV_KEY_TASK_SCHEDULES + i));
	}
	for (int i = CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE; i < ARRAY_SIZE(schedules); i++) {
		zassert_false(kv_store_key_exists(KV_KEY_TASK_SCHEDULES + i));
	}
}

ZTEST(task_runner_schedules_kv, test_schedules_kv_load_corrupt)
{
	struct task_schedule schedules[5] = {0};
	struct task_schedule schedule_null = {0};
	int num_eval;

	/* Write 5 schedules to the KV store */
	for (int i = 0; i < 5; i++) {
		schedules[i].validity = TASK_VALID_ACTIVE;
		schedules[i].periodicity_type = TASK_PERIODICITY_LOCKOUT;
		schedules[i].periodicity.lockout.lockout_s = 50 - i;
	}
	num_eval = task_runner_schedules_load(20, schedules, ARRAY_SIZE(schedules), out_schedules);
	zassert_equal(ARRAY_SIZE(schedules), num_eval);

	/* Intentionally corrupt stored schedule 3 */
	zassert_equal(10, kv_store_write(KV_KEY_TASK_SCHEDULES + 2, &schedules[2], 10));

	/* Load schedules again */
	num_eval = task_runner_schedules_load(20, schedules, ARRAY_SIZE(schedules), out_schedules);
	zassert_equal(ARRAY_SIZE(schedules), num_eval);
	for (int i = 0; i < ARRAY_SIZE(schedules); i++) {
		if (i == 2) {
			/* Schedule 3 should be zeroed out */
			zassert_mem_equal(&schedule_null, &out_schedules[i],
					  sizeof(struct task_schedule));
		} else {
			zassert_mem_equal(&schedules[i], &out_schedules[i],
					  sizeof(struct task_schedule));
		}
	}

	/* Intentionally corrupt last schedule */
	zassert_equal(10, kv_store_write(KV_KEY_TASK_SCHEDULES + 4, &schedules[4], 10));
	num_eval = task_runner_schedules_load(20, schedules, ARRAY_SIZE(schedules), out_schedules);
	zassert_equal(ARRAY_SIZE(schedules) - 1, num_eval);
}

void test_init(void *fixture)
{
	memset(out_schedules, 0x00, sizeof(out_schedules));
	kv_store_reset();
}

ZTEST_SUITE(task_runner_schedules_kv, NULL, NULL, test_init, NULL, NULL);
