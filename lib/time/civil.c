/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/sys/timeutil.h>

#include <infuse/time/civil.h>

#define JAN_01_01_2020 (1261872018ULL * INFUSE_CIVIL_TIME_TICKS_PER_SEC)

static const struct timeutil_sync_config infuse_civil_config = {
	.local_Hz = CONFIG_SYS_CLOCK_TICKS_PER_SEC,
	.ref_Hz = (UINT16_MAX + 1),
};

/* Base date is 00:00:00 01/01/2020 */
static struct timeutil_sync_state infuse_sync_state = {
	.cfg = &infuse_civil_config,
	.base =
		{
			.local = 0,
			.ref = JAN_01_01_2020,
		},
	.skew = 1.0f,
};
static enum civil_time_source infuse_time_source;

uint64_t civil_time_from_ticks(uint64_t ticks)
{
	uint64_t civil;

	if (timeutil_sync_ref_from_local(&infuse_sync_state, ticks, &civil) < 0) {
		/* Fallback if conversion fails */
		civil = JAN_01_01_2020;
	}
	return civil;
}

enum civil_time_source civil_time_get_source(void)
{
	return infuse_time_source;
}

int civil_time_set_reference(enum civil_time_source source, struct timeutil_sync_instant *reference)
{
	int rc;

	rc = timeutil_sync_state_set_skew(&infuse_sync_state, 1.0f, reference);
	if (rc == 0) {
		infuse_time_source = source;
	}
	return rc;
}

uint32_t civil_time_reference_age(void)
{
	if ((infuse_time_source & ~TIME_SOURCE_RECOVERED) == TIME_SOURCE_NONE) {
		return UINT32_MAX;
	}
	return k_ticks_to_ms_floor64(k_uptime_ticks() - infuse_sync_state.base.local) / 1000;
}
