/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/math/common.h>
#include <infuse/time/epoch.h>
#include <infuse/time/timezone.h>
#include <infuse/zbus/channels.h>

static void new_location_data(const struct zbus_channel *chan);

INFUSE_ZBUS_CHAN_DECLARE(INFUSE_ZBUS_CHAN_LOCATION);
ZBUS_LISTENER_DEFINE(location_data_listener, new_location_data);
ZBUS_CHAN_ADD_OBS(INFUSE_ZBUS_NAME(INFUSE_ZBUS_CHAN_LOCATION), location_data_listener, 5);

static int16_t current_timezone_minutes = INT16_MIN;
static int8_t current_timezone = INT8_MIN;

LOG_MODULE_REGISTER(loc_tz, CONFIG_LOCATION_TIMEZONE_LOG_LEVEL);

int location_timezone(int8_t *timezone)
{
	if (current_timezone == INT8_MIN) {
		/* Local timezone not known */
		return -EAGAIN;
	}
	*timezone = current_timezone;
	return 0;
}

int location_local_time(uint32_t *local_time_seconds)
{
	int32_t timezone_offset;

	if (current_timezone == INT8_MIN) {
		/* Local timezone not known */
		return -EAGAIN;
	}
	timezone_offset = current_timezone * SEC_PER_HOUR;
	*local_time_seconds = epoch_time_seconds(epoch_time_now()) + timezone_offset;
	return 0;
}

static void new_location_data(const struct zbus_channel *chan)
{
	const uint32_t threshold_mm = CONFIG_INFUSE_AUTO_LOCATION_TIMEZONE_REQUIRED_ACCURACY * 1000;
	const struct tdf_gcs_wgs84_llha *location = zbus_chan_const_msg(chan);
	int16_t new_timezone_minutes;

	/* Require accuracy for timezoning */
	if (location->h_acc > threshold_mm) {
		LOG_DBG("Insufficient location accuracy");
		return;
	}

	new_timezone_minutes =
		utc_timezone_minutes_location_approximate(location->location.longitude);
	if (math_abs(current_timezone_minutes - new_timezone_minutes) > 5) {
		/* Only update the hour timezone if we've moved by at least 5 minutes */
		current_timezone = utc_timezone_location_approximate(location->location.longitude);
		current_timezone_minutes = new_timezone_minutes;
		LOG_INF("Approximate timezone: %+d hrs", current_timezone);
	}
}
