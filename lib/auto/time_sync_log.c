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
#include <infuse/tdf/util.h>
#include <infuse/time/epoch.h>
#include <infuse/reboot.h>

struct auto_time_sync_state {
	struct epoch_time_cb callback;
	uint8_t logger_mask;
	uint8_t flags;
};

static struct auto_time_sync_state state;

static void reference_time_updated(enum epoch_time_source source, struct timeutil_sync_instant old,
				   struct timeutil_sync_instant new, void *user_ctx)
{
	struct auto_time_sync_state *log_state = user_ctx;
	int64_t diff_us;
	int64_t diff;

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

		TDF_DATA_LOGGER_LOG(log_state->logger_mask, TDF_TIME_SYNC, epoch_time_now(),
				    &tdf_sync);
	}

	if (log_state->flags & AUTO_TIME_SYNC_LOG_REBOOT_ON_SYNC) {
		/* Log the reboot again now we know the time */
		tdf_reboot_info_log(log_state->logger_mask);
		/* Don't run again */
		log_state->flags &= ~AUTO_TIME_SYNC_LOG_REBOOT_ON_SYNC;
	}
}

void auto_time_sync_log_configure(uint8_t tdf_logger_mask, uint8_t flags)
{
	/* Check if time is currently known */
	if (epoch_time_trusted_source(epoch_time_get_source(), true)) {
		/* Time is already known, no need to re-log reboot events */
		flags &= ~AUTO_TIME_SYNC_LOG_REBOOT_ON_SYNC;
	}

	state.callback.reference_time_updated = reference_time_updated;
	state.callback.user_ctx = &state;
	state.logger_mask = tdf_logger_mask;
	state.flags = flags;
	epoch_time_register_callback(&state.callback);
}
