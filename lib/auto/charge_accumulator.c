/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdbool.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>
#include <infuse/time/epoch.h>
#include <infuse/zbus/channels.h>

static void new_battery_data(const struct zbus_channel *chan);

INFUSE_ZBUS_CHAN_DECLARE(INFUSE_ZBUS_CHAN_BATTERY);
ZBUS_LISTENER_DEFINE(battery_listener, new_battery_data);
ZBUS_CHAN_ADD_OBS(INFUSE_ZBUS_NAME(INFUSE_ZBUS_CHAN_BATTERY), battery_listener, 10);

static struct k_spinlock lock;
static k_ticks_t last_measurement;
static int64_t microamp_ticks;
static uint32_t measurements;

static void new_battery_data(const struct zbus_channel *chan)
{
	const struct tdf_battery_state *bat = zbus_chan_const_msg(chan);
	k_ticks_t pub = zbus_chan_pub_stats_last_time(chan);
	k_ticks_t diff;

	K_SPINLOCK(&lock) {
		diff = pub - last_measurement;
		microamp_ticks += (bat->current_ua * diff);
		last_measurement = pub;
		measurements += 1;
	}
}

int64_t auto_charge_accumulator_query(uint32_t *num_measurements)
{
	int64_t microamp_seconds;

	K_SPINLOCK(&lock) {
		microamp_seconds = microamp_ticks / CONFIG_SYS_CLOCK_TICKS_PER_SEC;
		microamp_ticks = 0;
		if (num_measurements) {
			*num_measurements = measurements;
		}
		measurements = 0;
	}

	return microamp_seconds;
}
