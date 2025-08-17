/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/random/random.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/pm/device_runtime.h>

#include <infuse/shared/device.h>

#define PRIO_LOW  10
#define PRIO_HIGH 40

static const struct device *dev = DEVICE_DT_GET_ANY(zephyr_spdt_switch);
static const struct gpio_dt_spec control =
	GPIO_DT_SPEC_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(zephyr_spdt_switch), ctrl_gpios);

ZTEST(drivers_spdt_switch, test_power_domain)
{
#ifdef CONFIG_PM_DEVICE_RUNTIME
	gpio_flags_t flags;

	zassert_true(device_is_ready(dev));
	zassert_not_null(control.port);

	/* Default state, not powered, control line should not be driven */
	zassert_equal(0, gpio_pin_get_config_dt(&control, &flags));
	zassert_false(flags & GPIO_INPUT);
	zassert_false(flags & GPIO_OUTPUT);

	/* Power up switch directly, state should still not be driven */
	zassert_equal(0, pm_device_runtime_get(dev));
	zassert_equal(0, gpio_pin_get_config_dt(&control, &flags));
	zassert_false(flags & GPIO_INPUT);
	zassert_false(flags & GPIO_OUTPUT);

	/* Release switch, state should still not be driven */
	zassert_equal(0, pm_device_runtime_get(dev));
	zassert_equal(0, gpio_pin_get_config_dt(&control, &flags));
	zassert_false(flags & GPIO_INPUT);
	zassert_false(flags & GPIO_OUTPUT);
#endif /* CONFIG_PM_DEVICE_RUNTIME */
}

ZTEST(drivers_spdt_switch, test_active)
{
	gpio_flags_t flags;

	zassert_true(device_is_ready(dev));
	zassert_not_null(control.port);

	/* Request GPIO active, low priority */
	zassert_equal(0, shared_device_request(dev, PRIO_LOW, 1));
	zassert_equal(0, gpio_pin_get_config_dt(&control, &flags));
	zassert_false(flags & GPIO_INPUT);
	zassert_true(flags & GPIO_OUTPUT);
	zassert_true(flags & GPIO_OUTPUT_HIGH);

	/* Release request, back to floating */
	zassert_equal(0, shared_device_release(dev, PRIO_LOW));
	zassert_equal(0, gpio_pin_get_config_dt(&control, &flags));
	zassert_false(flags & GPIO_INPUT);
	zassert_false(flags & GPIO_OUTPUT);

	/* Request GPIO active, high priority */
	zassert_equal(0, shared_device_request(dev, PRIO_HIGH, 1));
	zassert_equal(0, gpio_pin_get_config_dt(&control, &flags));
	zassert_false(flags & GPIO_INPUT);
	zassert_true(flags & GPIO_OUTPUT);
	zassert_true(flags & GPIO_OUTPUT_HIGH);

	/* Release request, back to floating */
	zassert_equal(0, shared_device_release(dev, PRIO_HIGH));
	zassert_equal(0, gpio_pin_get_config_dt(&control, &flags));
	zassert_false(flags & GPIO_INPUT);
	zassert_false(flags & GPIO_OUTPUT);
}

ZTEST(drivers_spdt_switch, test_inactive)
{
	gpio_flags_t flags;

	zassert_true(device_is_ready(dev));
	zassert_not_null(control.port);

	/* Request GPIO active, low priority */
	zassert_equal(0, shared_device_request(dev, PRIO_LOW, 0));
	zassert_equal(0, gpio_pin_get_config_dt(&control, &flags));
	zassert_false(flags & GPIO_INPUT);
	zassert_true(flags & GPIO_OUTPUT);
	zassert_true(flags & GPIO_OUTPUT_LOW);

	/* Release request, back to floating */
	zassert_equal(0, shared_device_release(dev, PRIO_LOW));
	zassert_equal(0, gpio_pin_get_config_dt(&control, &flags));
	zassert_false(flags & GPIO_INPUT);
	zassert_false(flags & GPIO_OUTPUT);

	/* Request GPIO active, high priority */
	zassert_equal(0, shared_device_request(dev, PRIO_HIGH, 0));
	zassert_equal(0, gpio_pin_get_config_dt(&control, &flags));
	zassert_false(flags & GPIO_INPUT);
	zassert_true(flags & GPIO_OUTPUT);
	zassert_true(flags & GPIO_OUTPUT_LOW);

	/* Release request, back to floating */
	zassert_equal(0, shared_device_release(dev, PRIO_HIGH));
	zassert_equal(0, gpio_pin_get_config_dt(&control, &flags));
	zassert_false(flags & GPIO_INPUT);
	zassert_false(flags & GPIO_OUTPUT);
}

ZTEST(drivers_spdt_switch, test_priority_override_ordered)
{
	gpio_flags_t flags;

	zassert_true(device_is_ready(dev));
	zassert_not_null(control.port);

	/* Request GPIO active, low priority */
	zassert_equal(0, shared_device_request(dev, PRIO_LOW, 1));
	zassert_equal(0, gpio_pin_get_config_dt(&control, &flags));
	zassert_false(flags & GPIO_INPUT);
	zassert_true(flags & GPIO_OUTPUT);
	zassert_true(flags & GPIO_OUTPUT_HIGH);

	/* Request GPIO inactive, high priority */
	zassert_equal(0, shared_device_request(dev, PRIO_HIGH, 0));
	zassert_equal(0, gpio_pin_get_config_dt(&control, &flags));
	zassert_false(flags & GPIO_INPUT);
	zassert_true(flags & GPIO_OUTPUT);
	zassert_true(flags & GPIO_OUTPUT_LOW);

	/* Release high priority request */
	zassert_equal(0, shared_device_release(dev, PRIO_HIGH));
	zassert_equal(0, gpio_pin_get_config_dt(&control, &flags));
	zassert_false(flags & GPIO_INPUT);
	zassert_true(flags & GPIO_OUTPUT);
	zassert_true(flags & GPIO_OUTPUT_HIGH);

	/* Release low priority request */
	zassert_equal(0, shared_device_release(dev, PRIO_LOW));
	zassert_equal(0, gpio_pin_get_config_dt(&control, &flags));
	zassert_false(flags & GPIO_INPUT);
	zassert_false(flags & GPIO_OUTPUT);
}

ZTEST(drivers_spdt_switch, test_priority_override_unordered)
{
	gpio_flags_t flags;

	zassert_true(device_is_ready(dev));
	zassert_not_null(control.port);

	/* Request GPIO inactive, low priority */
	zassert_equal(0, shared_device_request(dev, PRIO_LOW, 0));
	zassert_equal(0, gpio_pin_get_config_dt(&control, &flags));
	zassert_false(flags & GPIO_INPUT);
	zassert_true(flags & GPIO_OUTPUT);
	zassert_true(flags & GPIO_OUTPUT_LOW);

	/* Request GPIO active, high priority */
	zassert_equal(0, shared_device_request(dev, PRIO_HIGH, 1));
	zassert_equal(0, gpio_pin_get_config_dt(&control, &flags));
	zassert_false(flags & GPIO_INPUT);
	zassert_true(flags & GPIO_OUTPUT);
	zassert_true(flags & GPIO_OUTPUT_HIGH);

	/* Release low priority request */
	zassert_equal(0, shared_device_release(dev, PRIO_LOW));
	zassert_equal(0, gpio_pin_get_config_dt(&control, &flags));
	zassert_false(flags & GPIO_INPUT);
	zassert_true(flags & GPIO_OUTPUT);
	zassert_true(flags & GPIO_OUTPUT_HIGH);

	/* Release high priority request */
	zassert_equal(0, shared_device_release(dev, PRIO_HIGH));
	zassert_equal(0, gpio_pin_get_config_dt(&control, &flags));
	zassert_false(flags & GPIO_INPUT);
	zassert_false(flags & GPIO_OUTPUT);
}

ZTEST(drivers_spdt_switch, test_dt_api)
{
	const struct shared_device_dt_spec u1_shared =
		SHARED_DEVICE_DT_SPEC_GET(DT_NODELABEL(user_1), rf_switch);
	const struct shared_device_dt_spec u2_shared =
		SHARED_DEVICE_DT_SPEC_GET(DT_NODELABEL(user_2), rf_switch);
	const struct shared_device_dt_spec u1_none =
		SHARED_DEVICE_DT_SPEC_GET_OR(DT_NODELABEL(user_2), no_prop, {0});
	gpio_flags_t flags;

	zassert_not_null(u1_shared.shared);
	zassert_not_null(u2_shared.shared);
	zassert_is_null(u1_none.shared);

	/* All is ready should pass (even the NULL device) */
	zassert_true(shared_device_is_ready_dt(&u1_shared));
	zassert_true(shared_device_is_ready_dt(&u2_shared));
	zassert_true(shared_device_is_ready_dt(&u1_none));

	/* All operations on NULL device should pass */
	zassert_equal(0, shared_device_request_dt(&u1_none));
	zassert_equal(0, shared_device_release_dt(&u1_none));

	/* We expect user 2 to have priority with the active state */
	zassert_equal(0, shared_device_request_dt(&u1_shared));
	zassert_equal(0, shared_device_request_dt(&u2_shared));
	zassert_equal(0, gpio_pin_get_config_dt(&control, &flags));
	zassert_false(flags & GPIO_INPUT);
	zassert_true(flags & GPIO_OUTPUT);
	zassert_true(flags & GPIO_OUTPUT_HIGH);

	zassert_equal(0, shared_device_release_dt(&u2_shared));
	zassert_equal(0, gpio_pin_get_config_dt(&control, &flags));
	zassert_false(flags & GPIO_INPUT);
	zassert_true(flags & GPIO_OUTPUT);
	zassert_true(flags & GPIO_OUTPUT_LOW);

	zassert_equal(0, shared_device_release_dt(&u1_shared));
	zassert_equal(0, gpio_pin_get_config_dt(&control, &flags));
	zassert_false(flags & GPIO_INPUT);
	zassert_false(flags & GPIO_OUTPUT);
}

ZTEST(drivers_spdt_switch, test_errors)
{
	zassert_true(device_is_ready(dev));
	zassert_not_null(control.port);

	/* Invalid state request */
	zassert_equal(-EINVAL, shared_device_request(dev, PRIO_LOW, 2));

	/* Request same state with different priority, inactive */
	zassert_equal(0, shared_device_request(dev, PRIO_LOW, 0));
	zassert_equal(-EALREADY, shared_device_request(dev, PRIO_HIGH, 0));
	zassert_equal(-EINVAL, shared_device_release(dev, PRIO_HIGH));
	zassert_equal(0, shared_device_release(dev, PRIO_LOW));

	/* Request same state with different priority, active */
	zassert_equal(0, shared_device_request(dev, PRIO_LOW, 1));
	zassert_equal(-EALREADY, shared_device_request(dev, PRIO_HIGH, 1));
	zassert_equal(-EINVAL, shared_device_release(dev, PRIO_HIGH));
	zassert_equal(0, shared_device_release(dev, PRIO_LOW));
}

ZTEST_SUITE(drivers_spdt_switch, NULL, NULL, NULL, NULL, NULL);
