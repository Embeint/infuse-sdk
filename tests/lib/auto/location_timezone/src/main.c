/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/auto/location_timezone.h>
#include <infuse/zbus/channels.h>

INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_LOCATION);
#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_LOCATION)

ZTEST(location_timezone, test_timezone)
{
	struct tdf_gcs_wgs84_llha llha = {0};
	uint32_t local_time;
	uint32_t utc_time;
	int8_t timezone;

	/* Initial state */
	zassert_equal(-EAGAIN, location_timezone(&timezone));
	zassert_equal(-EAGAIN, location_local_time(&local_time));

	/* Bad accuracy */
	llha.location.longitude = 0;
	llha.h_acc = (CONFIG_INFUSE_AUTO_LOCATION_TIMEZONE_REQUIRED_ACCURACY * 1000) + 1;
	zassert_equal(0, zbus_chan_pub(ZBUS_CHAN, &llha, K_NO_WAIT));
	zassert_equal(-EAGAIN, location_timezone(&timezone));
	zassert_equal(-EAGAIN, location_local_time(&local_time));

	/* Good accuracy */
	llha.h_acc = (5 * 1000);
	zassert_equal(0, zbus_chan_pub(ZBUS_CHAN, &llha, K_NO_WAIT));
	zassert_equal(0, location_timezone(&timezone));
	zassert_equal(0, location_local_time(&utc_time));
	zassert_equal(0, timezone);
	zassert_not_equal(0, utc_time);

	/* Shift timezone to border of +1 hour */
	llha.location.longitude = 224999999;
	zassert_equal(0, zbus_chan_pub(ZBUS_CHAN, &llha, K_NO_WAIT));
	zassert_equal(0, location_timezone(&timezone));
	zassert_equal(0, location_local_time(&local_time));
	zassert_equal(1, timezone);
	zassert_equal(utc_time + SEC_PER_HOUR, local_time);

	/* Shift to just the other side doesn't oscillate back and forth */
	llha.location.longitude = 225000001;
	zassert_equal(0, zbus_chan_pub(ZBUS_CHAN, &llha, K_NO_WAIT));
	zassert_equal(0, location_timezone(&timezone));
	zassert_equal(0, location_local_time(&local_time));
	zassert_equal(1, timezone);
	zassert_equal(utc_time + SEC_PER_HOUR, local_time);

	/* Shift enough and it will update */
	llha.location.longitude = 245000000;
	zassert_equal(0, zbus_chan_pub(ZBUS_CHAN, &llha, K_NO_WAIT));
	zassert_equal(0, location_timezone(&timezone));
	zassert_equal(0, location_local_time(&local_time));
	zassert_equal(2, timezone);
	zassert_equal(utc_time + (2 * SEC_PER_HOUR), local_time);

	/* Big jump with bad accuracy ignored */
	llha.location.longitude = -245000000;
	llha.h_acc = (CONFIG_INFUSE_AUTO_LOCATION_TIMEZONE_REQUIRED_ACCURACY * 1000) + 1;
	zassert_equal(0, zbus_chan_pub(ZBUS_CHAN, &llha, K_NO_WAIT));
	zassert_equal(0, location_timezone(&timezone));
	zassert_equal(0, location_local_time(&local_time));
	zassert_equal(2, timezone);
	zassert_equal(utc_time + (2 * SEC_PER_HOUR), local_time);

	/* Big jump immediately applied */
	llha.location.longitude = -245000000;
	llha.h_acc = (10 * 1000);
	zassert_equal(0, zbus_chan_pub(ZBUS_CHAN, &llha, K_NO_WAIT));
	zassert_equal(0, location_timezone(&timezone));
	zassert_equal(0, location_local_time(&local_time));
	zassert_equal(-2, timezone);
	zassert_equal(utc_time + (-2 * SEC_PER_HOUR), local_time);
}

ZTEST_SUITE(location_timezone, NULL, NULL, NULL, NULL, NULL);
