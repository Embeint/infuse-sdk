/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/logging/log.h>

#include <infuse/drivers/led/lp581x.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static int demo_pattern(const struct device *dev, const struct lp581x_animation_pattern *pattern,
			uint8_t num_patterns, const struct lp581x_animation_engines_config *engines,
			uint8_t leds, k_timeout_t duration)
{
	int rc;

	for (int i = 0; i < num_patterns; i++) {
		rc = lp581x_animation_pattern_program(dev, i, pattern + i);
		if (rc != 0) {
			LOG_ERR("Failed to program pattern %d (%d)", i, rc);
			return rc;
		}
	}

	rc = lp581x_animation_engines_configure(dev, engines);
	if (rc != 0) {
		LOG_ERR("Failed to configure animation engines (%d)", rc);
		return rc;
	}

	rc = lp581x_animation_start(dev, leds);
	if (rc != 0) {
		LOG_ERR("Failed to start animation (%d)", rc);
		return rc;
	}
	LOG_INF("Animation engine started");

	k_sleep(duration);

	rc = lp581x_animation_stop(dev);
	if (rc != 0) {
		LOG_ERR("Failed to stop animation (%d)", rc);
		return rc;
	}
	LOG_INF("Animation engine stopped");

	return 0;
}

static int blink_5hz(const struct device *dev, uint8_t leds, k_timeout_t duration)
{
	/* From EVK User Guide Table 3-2 */
	const struct lp581x_animation_pattern pattern = {
		.pre_pause =
			{
				.pwm = 0,
				.duration = LP581X_PHASE_0_MS,
			},
		.sloper = {.play_count = LP581X_PATTERN_PLAY_FOREVER,
			   .pwm = {255, 255, 0},
			   .duration = {LP581X_PHASE_100_MS, LP581X_PHASE_0_MS, LP581X_PHASE_100_MS,
					LP581X_PHASE_0_MS}},
		.post_pause =
			{
				.pwm = 0,
				.duration = LP581X_PHASE_0_MS,
			},
	};

	const struct lp581x_animation_engines_config engines = {
		.led_channel_engines = {LP581X_ENGINE_0, LP581X_ENGINE_0, LP581X_ENGINE_0},
		.num_engines = 1,
		.engines[0] =
			{
				.order = {LP581X_PATTERN_0, LP581X_PATTERN_SKIP,
					  LP581X_PATTERN_SKIP, LP581X_PATTERN_SKIP},
				.repeats = LP581X_ENGINE_REPEAT_FOREVER,
			},
	};

	return demo_pattern(dev, &pattern, 1, &engines, leds, duration);
}

static int breathing(const struct device *dev, uint8_t leds, k_timeout_t duration)
{
	/* From EVK User Guide Table 3-3 */
	const struct lp581x_animation_pattern pattern = {
		.pre_pause =
			{
				.pwm = 0,
				.duration = LP581X_PHASE_0_MS,
			},
		.sloper = {.play_count = LP581X_PATTERN_PLAY_FOREVER,
			   .pwm = {255, 255, 0},
			   .duration = {LP581X_PHASE_1000_MS, LP581X_PHASE_200_MS,
					LP581X_PHASE_1000_MS, LP581X_PHASE_200_MS}},
		.post_pause =
			{
				.pwm = 0,
				.duration = LP581X_PHASE_0_MS,
			},
	};

	const struct lp581x_animation_engines_config engines = {
		.led_channel_engines = {LP581X_ENGINE_0, LP581X_ENGINE_0, LP581X_ENGINE_0},
		.num_engines = 1,
		.engines[0] =
			{
				.order = {LP581X_PATTERN_0, LP581X_PATTERN_SKIP,
					  LP581X_PATTERN_SKIP, LP581X_PATTERN_SKIP},
				.repeats = LP581X_ENGINE_REPEAT_FOREVER,
			},
	};

	return demo_pattern(dev, &pattern, 1, &engines, leds, duration);
}

static int rainbow(const struct device *dev, k_timeout_t duration)
{
	const struct lp581x_animation_pattern patterns[2] = {
		{
			.pre_pause =
				{
					.pwm = 0,
					.duration = LP581X_PHASE_200_MS,
				},
			.sloper = {.play_count = 1,
				   .pwm = {160, 255, 160},
				   .duration = {LP581X_PHASE_300_MS, LP581X_PHASE_200_MS,
						LP581X_PHASE_200_MS, LP581X_PHASE_300_MS}},
			.post_pause =
				{
					.pwm = 0,
					.duration = LP581X_PHASE_0_MS,
				},
		},
		{
			.pre_pause =
				{
					.pwm = 0,
					.duration = LP581X_PHASE_1000_MS,
				},
		}};
	const struct lp581x_animation_engines_config engines = {
		.led_channel_engines = {LP581X_ENGINE_0, LP581X_ENGINE_1, LP581X_ENGINE_2},
		.num_engines = 3,
		.engines[0] =
			{
				.order = {LP581X_PATTERN_0, LP581X_PATTERN_1, LP581X_PATTERN_1,
					  LP581X_PATTERN_SKIP},
				.repeats = LP581X_ENGINE_REPEAT_FOREVER,
			},
		.engines[1] =
			{
				.order = {LP581X_PATTERN_1, LP581X_PATTERN_0, LP581X_PATTERN_1,
					  LP581X_PATTERN_SKIP},
				.repeats = LP581X_ENGINE_REPEAT_FOREVER,
			},
		.engines[2] =
			{
				.order = {LP581X_PATTERN_1, LP581X_PATTERN_1, LP581X_PATTERN_0,
					  LP581X_PATTERN_SKIP},
				.repeats = LP581X_ENGINE_REPEAT_FOREVER,
			},
	};

	return demo_pattern(dev, patterns, 2, &engines, 0x07, duration);
}

int main(void)
{
	const struct device *dev = DEVICE_DT_GET(DT_ALIAS(led_controller0));
	/* clang-format off */
	const struct led_dt_spec leds[] = {
		DT_FOREACH_CHILD_SEP(DT_ALIAS(led_controller0), LED_DT_SPEC_GET, (,))};
	/* clang-format on */

	/* Power up device */
	if (pm_device_runtime_get(dev) < 0) {
		LOG_ERR("Failed to power up %s", dev->name);
		return -ENODEV;
	}

	/* Channel 1 5Hz */
	if (blink_5hz(dev, 0x2, K_SECONDS(5)) < 0) {
		return -EIO;
	}
	k_sleep(K_SECONDS(1));

	/* Channels 0,1,2 breathing */
	if (breathing(dev, 0x7, K_SECONDS(5)) < 0) {
		return -EIO;
	}
	k_sleep(K_SECONDS(1));

	/* Channel combinations */
	if (rainbow(dev, K_SECONDS(10)) < 0) {
		return -EIO;
	}
	k_sleep(K_SECONDS(1));

	/* Revert to basic blinking forever */
	for (;;) {
		led_on_dt(&leds[0]);
		k_sleep(K_SECONDS(1));
		led_off_dt(&leds[0]);
		k_sleep(K_SECONDS(1));
	}
	return 0;
}
