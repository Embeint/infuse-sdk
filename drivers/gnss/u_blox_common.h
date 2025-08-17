/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_DRIVERS_GNSS_U_BLOX_COMMON_H_
#define INFUSE_SDK_DRIVERS_GNSS_U_BLOX_COMMON_H_

#include <zephyr/drivers/gpio.h>
#include <zephyr/modem/pipe.h>
#include <zephyr/pm/device.h>

#include <infuse/shared/device.h>
#include <infuse/gnss/ubx/modem.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(CONFIG_GNSS_U_BLOX_NO_API_COMPAT) ||                                                  \
	defined(CONFIG_GNSS_U_BLOX_CONSTELLATION_API_COMPAT)
#define GNSS_UBX_CONSTELLATION_CONFIG 1
#endif

struct ubx_common_pm_fn {
	int (*software_standby)(const struct device *dev);
	int (*software_resume)(const struct device *dev);
	int (*port_setup)(const struct device *dev, bool hardware_reset);
};

struct ubx_common_config {
	struct ubx_common_pm_fn pm_funcs;
	struct shared_device_dt_spec ant_switch;
	struct gpio_dt_spec reset_gpio;
	struct gpio_dt_spec extint_gpio;
	struct gpio_dt_spec timepulse_gpio;
	struct gpio_dt_spec data_ready_gpio;
	uint8_t data_ready_pio;
};

struct ubx_common_data {
	/* UBX modem data */
	struct ubx_modem_data modem;
	/* Callback for data ready GPIO */
	struct gpio_callback timepulse_cb;
	/* Handler for receiver changing states */
	struct ubx_message_handler_ctx mon_rxr_handler;
	/* Signal for MON-RXR handler */
	struct k_poll_signal mon_rxr_signal;
	/* Timestamp of the latest timepulse */
	k_ticks_t latest_timepulse;
	/* Wake time */
	k_timeout_t min_wake_time;
#ifndef CONFIG_GNSS_U_BLOX_NO_API_COMPAT
	/* NAV-PVT message handler */
	struct ubx_message_handler_ctx pvt_handler;
#ifdef CONFIG_GNSS_SATELLITES
	/* NAV-SAT message handler */
	struct ubx_message_handler_ctx sat_handler;
#endif /* CONFIG_GNSS_SATELLITES */
#endif /* CONFIG_GNSS_U_BLOX_NO_API_COMPAT */
};

#define UBX_COMMON_CONFIG_INST(inst, standby_fn, resume_fn, setup_fn)                              \
	{                                                                                          \
		.pm_funcs =                                                                        \
			{                                                                          \
				.software_standby = standby_fn,                                    \
				.software_resume = resume_fn,                                      \
				.port_setup = setup_fn,                                            \
			},                                                                         \
		.ant_switch = SHARED_DEVICE_DT_SPEC_INST_GET_OR(inst, antenna_switch, {0}),        \
		.reset_gpio = GPIO_DT_SPEC_INST_GET(inst, reset_gpios),                            \
		.extint_gpio = GPIO_DT_SPEC_INST_GET(inst, extint_gpios),                          \
		.timepulse_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, timepulse_gpios, {0}),            \
		.data_ready_gpio = GPIO_DT_SPEC_INST_GET(inst, data_ready_gpios),                  \
		.data_ready_pio = DT_INST_PROP(inst, data_ready_pio),                              \
	}

int ubx_common_init(const struct device *dev, struct modem_pipe *pipe);

void ubx_common_extint_wake(const struct device *dev);

int ubx_common_get_latest_timepulse(const struct device *dev, k_ticks_t *timestamp);

int ubx_common_pm_control(const struct device *dev, enum pm_device_action action);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_DRIVERS_GNSS_U_BLOX_COMMON_H_ */
