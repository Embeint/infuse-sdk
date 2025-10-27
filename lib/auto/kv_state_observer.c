/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/states.h>
#include <infuse/time/epoch.h>

static void kv_state_obs_value_changed(uint16_t key, const void *data, size_t data_len,
				       void *user_ctx);
static struct kv_store_cb kv_observer_cb = {
	.value_changed = kv_state_obs_value_changed,
};

#ifdef CONFIG_KV_STORE_KEY_LED_DISABLE_DAILY_TIME_RANGE
static void reference_time_updated(enum epoch_time_source source, struct timeutil_sync_instant old,
				   struct timeutil_sync_instant new, void *user_ctx);
static struct epoch_time_cb epoch_observer_cb = {
	.reference_time_updated = reference_time_updated,
};
static struct k_work_delayable led_delayable;
static uint32_t disable_daily_seconds_start;
static uint32_t disable_daily_seconds_end;
static bool has_disable_daily;

static uint32_t utc_seconds_from_hms(const struct kv_utc_hms *hms)
{
	return (hms->hour * SEC_PER_HOUR) + (hms->minute * SEC_PER_MIN) + hms->second;
}

static void led_disable_delayable(struct k_work *work)
{
	uint32_t utc_seconds;
	struct kv_utc_hms hms;
	uint32_t boundary;
	uint64_t now;
	struct tm c;

	/* If we don't know the time or have a KV, we can't suppress */
	if (!has_disable_daily || !epoch_time_trusted_source(epoch_time_get_source(), true)) {
		infuse_state_clear(INFUSE_STATE_LED_SUPPRESS);
		return;
	}

	/* Get current time */
	now = epoch_time_now();
	epoch_time_unix_calendar(now, &c);
	hms.hour = c.tm_hour;
	hms.minute = c.tm_min;
	hms.second = c.tm_sec;
	utc_seconds = utc_seconds_from_hms(&hms);

	/* Handle current time vs windows */
	if (disable_daily_seconds_start < disable_daily_seconds_end) {
		if (utc_seconds < disable_daily_seconds_start) {
			/* Before start window */
			boundary = disable_daily_seconds_start - utc_seconds;
			infuse_state_clear(INFUSE_STATE_LED_SUPPRESS);
		} else if (utc_seconds > disable_daily_seconds_end) {
			/* After end window */
			boundary = disable_daily_seconds_start + (SEC_PER_DAY - utc_seconds);
			infuse_state_clear(INFUSE_STATE_LED_SUPPRESS);
		} else {
			/* In range */
			boundary = disable_daily_seconds_end - utc_seconds;
			infuse_state_set(INFUSE_STATE_LED_SUPPRESS);
		}
	} else {
		if (utc_seconds <= disable_daily_seconds_end) {
			/* In range */
			boundary = disable_daily_seconds_end - utc_seconds;
			infuse_state_set(INFUSE_STATE_LED_SUPPRESS);
		} else if (utc_seconds >= disable_daily_seconds_start) {
			/* In range */
			boundary = (SEC_PER_DAY - utc_seconds) + disable_daily_seconds_end;
			infuse_state_set(INFUSE_STATE_LED_SUPPRESS);
		} else {
			/* Before start window */
			boundary = disable_daily_seconds_start - utc_seconds;
			infuse_state_clear(INFUSE_STATE_LED_SUPPRESS);
		}
	}

	/* Set reschedule time */
	boundary = MAX(1, boundary);
	k_work_reschedule(&led_delayable, K_SECONDS(boundary));
}

static void reference_time_updated(enum epoch_time_source source, struct timeutil_sync_instant old,
				   struct timeutil_sync_instant new, void *user_ctx)
{
	/* Re-evaluate immediately */
	k_work_reschedule(&led_delayable, K_NO_WAIT);
}

#endif /* CONFIG_KV_STORE_KEY_LED_DISABLE_DAILY_TIME_RANGE */

static void kv_state_obs_value_changed(uint16_t key, const void *data, size_t data_len,
				       void *user_ctx)
{
#ifdef CONFIG_KV_STORE_KEY_LED_DISABLE_DAILY_TIME_RANGE
	const struct kv_led_disable_daily_time_range *disable_daily;

	if (key == KV_KEY_LED_DISABLE_DAILY_TIME_RANGE) {
		if (data == NULL) {
			/* Slot has been deleted, cancel any suppression */
			has_disable_daily = false;
			k_work_cancel_delayable(&led_delayable);
			infuse_state_clear(INFUSE_STATE_LED_SUPPRESS);
		} else {
			disable_daily = data;
			/* Cache the current value */
			disable_daily_seconds_start =
				utc_seconds_from_hms(&disable_daily->disable_start);
			disable_daily_seconds_end =
				utc_seconds_from_hms(&disable_daily->disable_end);
			has_disable_daily = true;
			/* Re-evaluate immediately */
			k_work_reschedule(&led_delayable, K_NO_WAIT);
		}
	}
#endif /* CONFIG_KV_STORE_KEY_LED_DISABLE_DAILY_TIME_RANGE */
#ifdef CONFIG_KV_STORE_KEY_APPLICATION_ACTIVE
	const struct kv_application_active *active;

	if (key == KV_KEY_APPLICATION_ACTIVE) {
		if (data == NULL) {
			/* Slot has not been written, assume active */
			infuse_state_set(INFUSE_STATE_APPLICATION_ACTIVE);
		} else {
			active = data;
			infuse_state_set_to(INFUSE_STATE_APPLICATION_ACTIVE, active->active != 0);
		}
	}
#endif /* CONFIG_KV_STORE_KEY_APPLICATION_ACTIVE */
}

static int kv_state_observer_init(void)
{
	kv_store_register_callback(&kv_observer_cb);

#ifdef CONFIG_KV_STORE_KEY_LED_DISABLE_DAILY_TIME_RANGE
	struct kv_led_disable_daily_time_range disable_daily;

	epoch_time_register_callback(&epoch_observer_cb);
	k_work_init_delayable(&led_delayable, led_disable_delayable);
	/* Initialise the cached values */
	if (KV_STORE_READ(KV_KEY_LED_DISABLE_DAILY_TIME_RANGE, &disable_daily) ==
	    sizeof(disable_daily)) {
		disable_daily_seconds_start = utc_seconds_from_hms(&disable_daily.disable_start);
		disable_daily_seconds_end = utc_seconds_from_hms(&disable_daily.disable_end);
		has_disable_daily = true;
	}
	/* Evaluate immediately */
	k_work_schedule(&led_delayable, K_NO_WAIT);
#endif /* CONFIG_KV_STORE_KEY_LED_DISABLE_DAILY_TIME_RANGE */
#ifdef CONFIG_KV_STORE_KEY_APPLICATION_ACTIVE
	struct kv_application_active active;

	if (KV_STORE_READ(KV_KEY_APPLICATION_ACTIVE, &active) == sizeof(active)) {
		infuse_state_set_to(INFUSE_STATE_APPLICATION_ACTIVE, active.active != 0);
	} else {
		/* Slot has not been written, assume active */
		infuse_state_set(INFUSE_STATE_APPLICATION_ACTIVE);
	}
#else
	/* KV key is not enabled, assume active */
	infuse_state_set(INFUSE_STATE_APPLICATION_ACTIVE);
#endif /* CONFIG_KV_STORE_KEY_APPLICATION_ACTIVE */
	return 0;
}

SYS_INIT(kv_state_observer_init, APPLICATION, 0);
