/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/auto/charger_control.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/tdf/definitions.h>
#include <infuse/tdf/tdf.h>
#include <infuse/time/epoch.h>
#include <infuse/types.h>
#include <infuse/zbus/channels.h>

#define DT_DRV_COMPAT embeint_gpio_charger_control

const struct gpio_dt_spec control_gpio =
	GPIO_DT_SPEC_GET(DT_NODELABEL(charger_control), control_gpios);

INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_AMBIENT_ENV);
#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_AMBIENT_ENV)

static void expect_logging(uint8_t expected_state)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_charger_en_control *state;
	struct tdf_parsed tdf;
	struct net_buf *pkt;

	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	pkt = k_fifo_get(tx_queue, K_MSEC(10));
	zassert_not_null(pkt);

	net_buf_pull(pkt, sizeof(struct epacket_dummy_frame));
	zassert_equal(0, tdf_parse_find_in_buf(pkt->data, pkt->len, TDF_CHARGER_EN_CONTROL, &tdf));
	state = tdf.data;

	zassert_equal(expected_state, state->enabled);
	net_buf_unref(pkt);
}

ZTEST(charger_control, test_control)
{
	struct tdf_ambient_temp_pres_hum env = {.temperature = 25 * 1000};
	gpio_flags_t flags;

	/* On boot, pin should be output and active */
	zassert_equal(0, gpio_pin_get_config_dt(&control_gpio, &flags));
	zassert_true(flags & GPIO_OUTPUT);
	zassert_true(flags & GPIO_OUTPUT_INIT_HIGH);

	auto_charger_control_log_configure(TDF_DATA_LOGGER_SERIAL);

	/* From 25 down to -15 charger should remain enabled */
	while (env.temperature >= -15 * 1000) {
		zbus_chan_pub(ZBUS_CHAN, &env, K_FOREVER);

		/* Charger should remain enabled */
		zassert_equal(0, gpio_pin_get_config_dt(&control_gpio, &flags));
		zassert_true(flags & GPIO_OUTPUT_INIT_HIGH);

		env.temperature -= 1000;
	}

	/* At -16 the charger should be disabled */
	zbus_chan_pub(ZBUS_CHAN, &env, K_FOREVER);

	/* Charger should now be disabled */
	zassert_equal(0, gpio_pin_get_config_dt(&control_gpio, &flags));
	zassert_true(flags & GPIO_OUTPUT_INIT_LOW);
	expect_logging(0);

	/* Charger should not be re-enabled until we hit 10 degrees */
	while (env.temperature < -10 * 1000) {
		zbus_chan_pub(ZBUS_CHAN, &env, K_FOREVER);

		/* Charger should remain enabled */
		zassert_equal(0, gpio_pin_get_config_dt(&control_gpio, &flags));
		zassert_true(flags & GPIO_OUTPUT_INIT_LOW);

		env.temperature += 1000;
	}

	zbus_chan_pub(ZBUS_CHAN, &env, K_FOREVER);

	/* Charger now enabled again */
	zassert_equal(0, gpio_pin_get_config_dt(&control_gpio, &flags));
	zassert_true(flags & GPIO_OUTPUT_INIT_HIGH);
	expect_logging(1);

	/* Charger should remain enabled all the way to 75 degrees */
	while (env.temperature <= 75 * 1000) {
		zbus_chan_pub(ZBUS_CHAN, &env, K_FOREVER);

		/* Charger should remain enabled */
		zassert_equal(0, gpio_pin_get_config_dt(&control_gpio, &flags));
		zassert_true(flags & GPIO_OUTPUT_INIT_HIGH);

		env.temperature += 1000;
	}

	/* At 76 the charger should be disabled */
	zbus_chan_pub(ZBUS_CHAN, &env, K_FOREVER);

	/* Charger should now be disabled */
	zassert_equal(0, gpio_pin_get_config_dt(&control_gpio, &flags));
	zassert_true(flags & GPIO_OUTPUT_INIT_LOW);
	expect_logging(0);

	/* Charger should not be re-enabled until we hit 70 degrees */
	while (env.temperature > 70 * 1000) {
		zbus_chan_pub(ZBUS_CHAN, &env, K_FOREVER);

		/* Charger should remain enabled */
		zassert_equal(0, gpio_pin_get_config_dt(&control_gpio, &flags));
		zassert_true(flags & GPIO_OUTPUT_INIT_LOW);

		env.temperature -= 1000;
	}

	zbus_chan_pub(ZBUS_CHAN, &env, K_FOREVER);

	/* Charger now enabled again */
	zassert_equal(0, gpio_pin_get_config_dt(&control_gpio, &flags));
	zassert_true(flags & GPIO_OUTPUT_INIT_HIGH);
	expect_logging(1);
}

ZTEST_SUITE(charger_control, NULL, NULL, NULL, NULL, NULL);
