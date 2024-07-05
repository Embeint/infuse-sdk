/**
 * @file
 * @brief Infuse-IoT watchdog helpers
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 *
 * @details
 * Must only be included from files that declare a Zephyr logging context.
 * Watchdog configuration errors are both critical to find during development
 * and "invisible" if applications aren't explicit about displaying them, which
 * is why the logging occurs in this context.
 */

#ifndef INFUSE_SDK_DRIVERS_WATCHDOG_H_
#define INFUSE_SDK_DRIVERS_WATCHDOG_H_

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log_instance.h>

#include <zephyr/drivers/watchdog.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Infuse watchdog API
 * @defgroup infuse_watchdog_apis Infuse watchdog APIs
 * @{
 */

/* Infuse watchdog device */
#define INFUSE_WATCHDOG_DEV DEVICE_DT_GET(DT_ALIAS(watchdog0))

/* Maximum duration to sleep before waking up to feed watchdog */
#define INFUSE_WATCHDOG_FEED_PERIOD K_MSEC(CONFIG_INFUSE_WATCHDOG_PERIOD_MS - 100)

/** Default timeout configuration for subsystems */
#define INFUSE_WATCHDOG_DEFAULT_TIMEOUT_CFG                                                        \
	(struct wdt_timeout_cfg)                                                                   \
	{                                                                                          \
		.window =                                                                          \
			{                                                                          \
				.min = 0,                                                          \
				.max = CONFIG_INFUSE_WATCHDOG_PERIOD_MS,                           \
			},                                                                         \
		.flags = WDT_FLAG_RESET_SOC, .callback = infuse_watchdog_expired,                  \
	}

#if defined(CONFIG_INFUSE_WATCHDOG) || defined(__doxygen__)

#ifdef CONFIG_LOG
/* Forward declarations to enable `LOG_MODULE_DECLARE` to occur after this is included */
static const struct log_source_const_data *__log_current_const_data __unused;
static struct log_source_dynamic_data *__log_current_dynamic_data __unused;
static const uint32_t __log_level __unused;
#endif /* CONFIG_LOG */

#define INFUSE_WATCHDOG_REGISTER_SYS_INIT(name, dependency, chan_name, period_name)                \
	static k_timeout_t period_name = K_FOREVER;                                                \
	static int chan_name;                                                                      \
	static int name##_register(void)                                                           \
	{                                                                                          \
		(void)period_name;                                                                 \
		wdog_channel =                                                                     \
			IS_ENABLED(dependency) ? infuse_watchdog_install(&loop_period) : -ENODEV;  \
		return 0;                                                                          \
	}                                                                                          \
	SYS_INIT(name##_register, POST_KERNEL, 0);

/**
 * @brief Function that is called on watchdog expiry
 *
 * The standard implementation of this function is in lib/reboot.c
 *
 * @note With multiple channels installed with @ref INFUSE_WATCHDOG_REGISTER_SYS_INIT this
 *       function will be called multiple times.
 *
 * @param dev Watchdog instance that expired
 * @param channel_id Channel ID that expired
 */
void infuse_watchdog_expired(const struct device *dev, int channel_id);

/**
 * @brief Install an Infuse watchdog channel
 *
 * @return value from @ref wdt_install_timeout
 */
static inline int infuse_watchdog_install(k_timeout_t *feed_period)
{
	const struct wdt_timeout_cfg timeout_cfg = INFUSE_WATCHDOG_DEFAULT_TIMEOUT_CFG;
	int wdog_channel = wdt_install_timeout(INFUSE_WATCHDOG_DEV, &timeout_cfg);

	if (wdog_channel < 0) {
		if (wdog_channel == -EBUSY) {
			LOG_ERR("Attempted to allocate wdog channel after wdog started");
		} else if (wdog_channel == -ENOMEM) {
			LOG_ERR("Insufficient wdog channels");
		}
		*feed_period = K_FOREVER;
	} else {
		*feed_period = INFUSE_WATCHDOG_FEED_PERIOD;
	}
	return wdog_channel;
}

/**
 * @brief Start the Infuse watchdog
 *
 * @return value from @ref wdt_setup
 */
static inline int infuse_watchdog_start(void)
{
	int rc = wdt_setup(INFUSE_WATCHDOG_DEV, WDT_OPT_PAUSE_HALTED_BY_DBG);

	if (rc < 0) {
		LOG_ERR("Watchdog failed to start (%d)", rc);
	}
	return rc;
}

/**
 * @brief Feed an Infuse watchdog channel
 *
 * @param wdog_channel Channel from @ref infuse_watchdog_install
 */
static inline void infuse_watchdog_feed(int wdog_channel)
{
	/* Feed the watchdog */
	if (wdog_channel >= 0) {
		(void)wdt_feed(INFUSE_WATCHDOG_DEV, wdog_channel);
	}
}

#else

#define INFUSE_WATCHDOG_REGISTER_SYS_INIT(name, dependency, chan_name, period_name)                \
	static k_timeout_t period_name = K_FOREVER;                                                \
	static int chan_name = 0

static inline int infuse_watchdog_install(k_timeout_t *feed_period)
{
	return 0;
}

static inline int infuse_watchdog_start(void)
{
	return 0;
}

static inline void infuse_watchdog_feed(int wdog_channel)
{
}

#endif /* defined(CONFIG_INFUSE_WATCHDOG) || defined(__doxygen__) */

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_DRIVERS_WATCHDOG_H_ */
