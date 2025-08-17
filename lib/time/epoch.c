/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/sys/timeutil.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/logging/log.h>

#include <infuse/time/epoch.h>
#include <infuse/states.h>

#define JAN_01_01_2020 (1261872018ULL * INFUSE_EPOCH_TIME_TICKS_PER_SEC)

static const struct timeutil_sync_config infuse_civil_config = {
	.local_Hz = CONFIG_SYS_CLOCK_TICKS_PER_SEC,
	.ref_Hz = (UINT16_MAX + 1),
};

/* Base date is 00:00:00 01/01/2020 */
IF_DISABLED(CONFIG_ZTEST, (static))
struct timeutil_sync_state infuse_sync_state = {
	.cfg = &infuse_civil_config,
	.base =
		{
			.local = 0,
			.ref = JAN_01_01_2020,
		},
	.skew = 1.0f,
};
static enum epoch_time_source infuse_time_source;
static sys_slist_t cb_list;

LOG_MODULE_REGISTER(epoch_time, LOG_LEVEL_INF);

#ifdef CONFIG_ZTEST

void epoch_time_reset(void)
{
	infuse_sync_state.base.local = 0;
	infuse_sync_state.base.ref = JAN_01_01_2020;
	infuse_sync_state.skew = 1.0f;
	infuse_time_source = TIME_SOURCE_NONE;
	sys_slist_init(&cb_list);
}

#endif /* CONFIG_ZTEST */

void epoch_time_register_callback(struct epoch_time_cb *cb)
{
	bool found;
	/* Ensure callbacks aren't registered twice */
	found = sys_slist_find_and_remove(&cb_list, &cb->node);
	__ASSERT(found == false, "Callback %p added twice", cb);
	/* Append to list */
	sys_slist_append(&cb_list, &cb->node);
}

uint64_t ticks_from_epoch_time(uint64_t epoch_time)
{
	int64_t local = 0;

	(void)timeutil_sync_local_from_ref(&infuse_sync_state, epoch_time, &local);
	return local;
}

uint64_t epoch_time_from_ticks(uint64_t ticks)
{
	uint64_t civil;

	if (timeutil_sync_ref_from_local(&infuse_sync_state, ticks, &civil) < 0) {
		/* Reset to default state */
		infuse_sync_state.base.local = 0;
		infuse_sync_state.base.ref = JAN_01_01_2020;
		infuse_sync_state.skew = 1.0f;
		/* Reconvert */
		(void)timeutil_sync_ref_from_local(&infuse_sync_state, ticks, &civil);
	}
	return civil;
}

uint32_t epoch_period_from_array_ticks(uint64_t array_ticks, uint16_t array_num)
{
	if (array_num < 2) {
		return 0;
	}
	return k_ticks_to_epoch_near64(array_ticks) / (array_num - 1);
}

void epoch_time_unix_calendar(uint64_t epoch_time, struct tm *calendar)
{
	time_t unix_time = unix_time_from_epoch(epoch_time);

	*calendar = *gmtime(&unix_time);
}

enum epoch_time_source epoch_time_get_source(void)
{
	return infuse_time_source;
}

int epoch_time_set_reference(enum epoch_time_source source, struct timeutil_sync_instant *reference)
{
	struct timeutil_sync_instant prev = infuse_sync_state.base;
	struct epoch_time_cb *cb;
	int rc;

	/* Reference time of 0 is invalid */
	if (reference->ref == 0) {
		return -EINVAL;
	}

	/* Function only fails if `skew` is <= 0.0f */
	rc = timeutil_sync_state_set_skew(&infuse_sync_state, 1.0f, reference);
	__ASSERT_NO_MSG(rc == 0);
	infuse_time_source = source;

#ifdef CONFIG_INFUSE_APPLICATION_STATES
	if (epoch_time_trusted_source(source, true)) {
		/* Time is known from a trusted source */
		infuse_state_set(INFUSE_STATE_TIME_KNOWN);
	}
#endif /* CONFIG_INFUSE_APPLICATION_STATES */

	/* Notify interested parties of reference instant change */
	SYS_SLIST_FOR_EACH_CONTAINER(&cb_list, cb, node) {
		if (cb->reference_time_updated) {
			cb->reference_time_updated(source, prev, *reference, cb->user_ctx);
		}
	}
#ifdef CONFIG_INFUSE_EPOCH_TIME_PRINT_ON_SYNC
	uint64_t now = epoch_time_now();
	struct tm c;

	epoch_time_unix_calendar(now, &c);
	LOG_INF("Now: %d-%02d-%02dT%02d:%02d:%02d.%03d UTC", 1900 + c.tm_year, 1 + c.tm_mon,
		c.tm_mday, c.tm_hour, c.tm_min, c.tm_sec, epoch_time_milliseconds(now));
#endif /* CONFIG_INFUSE_EPOCH_TIME_PRINT_ON_SYNC */
	return rc;
}

uint32_t epoch_time_reference_age(void)
{
	if (!epoch_time_trusted_source(infuse_time_source, true)) {
		return UINT32_MAX;
	}
	return k_ticks_to_ms_floor64(k_uptime_ticks() - infuse_sync_state.base.local) / 1000;
}

int epoch_time_reference_shift(const struct timeutil_sync_instant *ref_a,
			       const struct timeutil_sync_instant *ref_b, int64_t *epoch_shift)
{
	struct timeutil_sync_state state_a = {
		.cfg = &infuse_civil_config,
		.base = *ref_a,
		.skew = 1.0f,
	};
	struct timeutil_sync_state state_b = {
		.cfg = &infuse_civil_config,
		.base = *ref_b,
		.skew = 1.0f,
	};
	uint64_t now = k_uptime_ticks();
	uint64_t out_a, out_b;

	if ((timeutil_sync_ref_from_local(&state_a, now, &out_a) < 0) ||
	    (timeutil_sync_ref_from_local(&state_b, now, &out_b) < 0)) {
		return -EINVAL;
	}
	*epoch_shift = (int64_t)out_b - (int64_t)out_a;
	return 0;
}
