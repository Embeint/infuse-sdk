/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <infuse/auto/time_sync_log.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>
#include <infuse/time/epoch.h>

static struct auto_time_sync_state {
	struct epoch_time_cb callback;
	uint8_t logger_mask;
	uint8_t flags;
} state;

static void reference_time_updated(enum epoch_time_source source, struct timeutil_sync_instant old,
				   struct timeutil_sync_instant new, void *user_ctx)
{
	struct auto_time_sync_state *log_state = user_ctx;
	int64_t diff, diff_us;

	if (log_state->flags & AUTO_TIME_SYNC_LOG_SYNCS) {
		/* Calculate time shift */
		epoch_time_reference_shift(&old, &new, &diff);
		if (diff >= 0) {
			diff_us = k_epoch_to_us_near64(diff);
		} else {
			diff_us = -k_epoch_to_us_near64(-diff);
		}

		/* Limit range to int32_t */
		diff_us = MAX(INT32_MIN, diff_us);
		diff_us = MIN(INT32_MAX, diff_us);

		/* Log to requested loggers */
		struct tdf_time_sync tdf_sync = {.source = source, .shift = diff_us};

		tdf_data_logger_log(log_state->logger_mask, TDF_TIME_SYNC, sizeof(tdf_sync),
				    epoch_time_now(), &tdf_sync);
	}
}

void auto_time_sync_log_configure(uint8_t tdf_logger_mask, uint8_t flags)
{
	state.callback.reference_time_updated = reference_time_updated;
	state.callback.user_ctx = &state;
	state.logger_mask = tdf_logger_mask;
	state.flags = flags;
	epoch_time_register_callback(&state.callback);
}
