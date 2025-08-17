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
#include <zephyr/drivers/gpio.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/auto/charge_accumulator.h>
#include <infuse/zbus/channels.h>

INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_BATTERY);
#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY)

ZTEST(charge_accumulator, test_accumulator)
{
	struct tdf_battery_state bat = {.voltage_mv = 3000, .current_ua = 0};
	int64_t charge;
	uint32_t num;
	int iter = 1;
	int rc;

	/* Initial state */
	charge = auto_charge_accumulator_query(&num);
	zassert_equal(0, num);
	zassert_equal(0, charge);

	/* No charging for 5 seconds */
	for (int i = 0; i < 5; i++) {
		rc = zbus_chan_pub(ZBUS_CHAN, &bat, K_NO_WAIT);
		zassert_equal(0, rc);
		k_sleep(K_TIMEOUT_ABS_SEC(iter));
		iter++;
	}

	charge = auto_charge_accumulator_query(&num);
	zassert_equal(5, num);
	zassert_equal(0, charge);

	/* 1 mA for 5 seconds == 5000 uA seconds */
	bat.current_ua = 1000;
	for (int i = 0; i < 5; i++) {
		rc = zbus_chan_pub(ZBUS_CHAN, &bat, K_NO_WAIT);
		zassert_equal(0, rc);
		k_sleep(K_TIMEOUT_ABS_SEC(iter));
		iter++;
	}
	charge = auto_charge_accumulator_query(NULL);
	zassert_equal(5000, charge);

	/* 10 mA for 10 seconds, -20mA for 10 seconds (-100000 uA seconds) */
	for (int i = 0; i < 20; i++) {
		bat.current_ua = i < 10 ? 10000 : -20000;
		rc = zbus_chan_pub(ZBUS_CHAN, &bat, K_NO_WAIT);
		zassert_equal(0, rc);
		k_sleep(K_TIMEOUT_ABS_SEC(iter));
		iter++;
	}
	charge = auto_charge_accumulator_query(&num);
	zassert_equal(20, num);
	zassert_equal(-100000, charge);

	/* Weird intervals (15 mA for 1 second), 1mA after 30 seconds (30000 uA seconds)  */
	bat.current_ua = 15000;
	rc = zbus_chan_pub(ZBUS_CHAN, &bat, K_NO_WAIT);
	zassert_equal(0, rc);
	k_sleep(K_TIMEOUT_ABS_SEC(iter));
	iter += 30;
	rc = zbus_chan_pub(ZBUS_CHAN, &bat, K_NO_WAIT);
	zassert_equal(0, rc);

	charge = auto_charge_accumulator_query(&num);
	zassert_equal(2, num);
	zassert_equal(30000, charge);
}

ZTEST_SUITE(charge_accumulator, NULL, NULL, NULL, NULL, NULL);
