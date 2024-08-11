/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/sys/timeutil.h>
#include <zephyr/logging/log.h>

#include <infuse/time/epoch.h>

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

void epoch_time_register_callback(struct epoch_time_cb *cb)
{
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

	rc = timeutil_sync_state_set_skew(&infuse_sync_state, 1.0f, reference);
	if (rc == 0) {
		infuse_time_source = source;
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
	}
	return rc;
}

uint32_t epoch_time_reference_age(void)
{
	if ((infuse_time_source & ~TIME_SOURCE_RECOVERED) == TIME_SOURCE_NONE) {
		return UINT32_MAX;
	}
	return k_ticks_to_ms_floor64(k_uptime_ticks() - infuse_sync_state.base.local) / 1000;
}
