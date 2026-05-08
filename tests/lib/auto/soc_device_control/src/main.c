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
#include <zephyr/device.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>

#include <infuse/auto/soc_device_control.h>
#include <infuse/zbus/channels.h>

static int test_driver_action(const struct device *dev, enum pm_device_action action)
{
	return 0;
}

static int test_driver_init(const struct device *dev)
{
	return 0;
}

PM_DEVICE_DEFINE(test_driver, test_driver_action, 0);
DEVICE_DEFINE(test_driver, "test_driver", &test_driver_init, PM_DEVICE_GET(test_driver), NULL, NULL,
	      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_BATTERY);
#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY)

ZTEST(soc_device_control, test_failures)
{
	const struct device *dev = &DEVICE_NAME_GET(test_driver);
	struct soc_device_control_state state = {
		.device = dev,
		.soc_enable = 25,
		.soc_disable = 20,
	};

	/* NULL state */
	zassert_equal(-EINVAL, soc_device_control_register(NULL));

	/* No device */
	state.device = NULL;
	zassert_equal(-ENODEV, soc_device_control_register(&state));
	state.device = dev;

	/* SoC's above 100 */
	state.soc_enable = 101;
	zassert_equal(-EINVAL, soc_device_control_register(&state));
	state.soc_enable = 25;
	state.soc_disable = 101;
	zassert_equal(-EINVAL, soc_device_control_register(&state));
	state.soc_disable = 25;

	/* Disable above enable */
	state.soc_disable = 30;
	zassert_equal(-EINVAL, soc_device_control_register(&state));
	state.soc_disable = 25;

	/* Unregister without faulting */
	zassert_false(soc_device_control_unregister(&state));
}

ZTEST(soc_device_control, test_standard)
{
	INFUSE_ZBUS_TYPE(INFUSE_ZBUS_CHAN_BATTERY) battery;
	const struct device *dev = &DEVICE_NAME_GET(test_driver);
	enum pm_device_state expected_state;
	enum pm_device_state pm_state;

	struct soc_device_control_state state = {
		.device = dev,
		.soc_enable = 25,
		.soc_disable = 20,
	};

	uint8_t soc_transitions[] = {10, 20, 24, 25, 26, 60, 100, 30, 20, 19, 3, 25};
	bool expected_active[] = {false, false, false, true,  true,  true,
				  true,  true,  true,  false, false, true};

	zassert_equal(ARRAY_SIZE(soc_transitions), ARRAY_SIZE(expected_active));

	zassert_true(pm_device_runtime_is_enabled(dev));
	zassert_equal(0, pm_device_state_get(dev, &pm_state));
	zassert_equal(PM_DEVICE_STATE_SUSPENDED, pm_state);

	/* Register for control */
	zassert_equal(0, soc_device_control_register(&state));

	for (int i = 0; i < ARRAY_SIZE(soc_transitions); i++) {
		battery.soc = soc_transitions[i];
		expected_state =
			expected_active[i] ? PM_DEVICE_STATE_ACTIVE : PM_DEVICE_STATE_SUSPENDED;
		zbus_chan_pub(ZBUS_CHAN, &battery, K_FOREVER);

		zassert_equal(0, pm_device_state_get(dev, &pm_state));
		zassert_equal(expected_state, pm_state);
	}

	/* Unregister for control */
	zassert_true(soc_device_control_unregister(&state));

	/* Unregistering released the claim */
	zassert_equal(0, pm_device_state_get(dev, &pm_state));
	zassert_equal(PM_DEVICE_STATE_SUSPENDED, pm_state);

	/* Additional unregistrations fail */
	zassert_false(soc_device_control_unregister(&state));

	/* Cycling a registration does nothing and doesn't fault */
	zassert_equal(0, soc_device_control_register(&state));
	zassert_equal(0, pm_device_state_get(dev, &pm_state));
	zassert_equal(PM_DEVICE_STATE_SUSPENDED, pm_state);
	zassert_true(soc_device_control_unregister(&state));
	zassert_equal(0, pm_device_state_get(dev, &pm_state));
	zassert_equal(PM_DEVICE_STATE_SUSPENDED, pm_state);
}

void *test_setup(void)
{
	const struct device *dev = &DEVICE_NAME_GET(test_driver);

	pm_device_runtime_enable(dev);
	return NULL;
}

ZTEST_SUITE(soc_device_control, NULL, test_setup, NULL, NULL, NULL);
