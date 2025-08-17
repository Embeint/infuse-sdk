/**
 * @file
 * @brief Infuse-IoT watchdog helpers
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
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
#include <zephyr/toolchain.h>

#include <zephyr/drivers/watchdog.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Infuse watchdog API
 * @defgroup infuse_watchdog_apis Infuse watchdog APIs
 * @{
 */

/** Infuse watchdog device */
#define INFUSE_WATCHDOG_DEV DEVICE_DT_GET(DT_ALIAS(watchdog0))

/** Maximum duration to sleep before waking up to feed watchdog */
#define INFUSE_WATCHDOG_FEED_PERIOD                                                                \
	K_MSEC(CONFIG_INFUSE_WATCHDOG_PERIOD_MS - CONFIG_INFUSE_WATCHDOG_FEED_EARLY_MS)

/** Watchdog expiry callback, if supported */
#define _INFUSE_WATCHDOG_CB                                                                        \
	COND_CODE_1(CONFIG_HAS_WDT_NO_CALLBACKS, (NULL), (infuse_watchdog_expired))

/** Default timeout configuration for subsystems */
#define INFUSE_WATCHDOG_DEFAULT_TIMEOUT_CFG                                                        \
	(struct wdt_timeout_cfg)                                                                   \
	{                                                                                          \
		.window =                                                                          \
			{                                                                          \
				.min = 0,                                                          \
				.max = CONFIG_INFUSE_WATCHDOG_PERIOD_MS,                           \
			},                                                                         \
		.flags = WDT_FLAG_RESET_SOC, .callback = _INFUSE_WATCHDOG_CB,                      \
	}

#if defined(CONFIG_INFUSE_WATCHDOG) || defined(__DOXYGEN__)

/**
 * @brief Install a watchdog timeout at boot
 *
 * @param name Unique prefix for constructed variables
 * @param dependency Timeout only installed if `IS_ENABLED(dependency)`
 * @param chan_name Name of the variable for the channel ID
 * @param period_name Name of the variable for the channel feed period
 */
#define INFUSE_WATCHDOG_REGISTER_SYS_INIT(name, dependency, chan_name, period_name)                \
	static k_timeout_t period_name = K_FOREVER;                                                \
	static int chan_name;                                                                      \
	static int name##_register(void)                                                           \
	{                                                                                          \
		(void)period_name;                                                                 \
		chan_name =                                                                        \
			IS_ENABLED(dependency) ? infuse_watchdog_install(&period_name) : -ENODEV;  \
		return 0;                                                                          \
	}                                                                                          \
	SYS_INIT(name##_register, POST_KERNEL, 0);

/**
 * @brief Function that is called just prior to watchdog expiry
 *
 * The standard implementation of this function is in lib/reboot.c
 *
 * @param dev Watchdog instance that is about to expire
 * @param channel_id Channel ID that is about to expire
 */
void infuse_watchdog_warning(const struct device *dev, int channel_id);

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
 * @return value from @a wdt_install_timeout
 */
int infuse_watchdog_install(k_timeout_t *feed_period);

/**
 * @brief Register a watchdog channel against a thread
 *
 * This allows thread state to be determined by @ref infuse_watchdog_thread_state_lookup
 * in the event that the channel expires.
 *
 * @note Also feeds the watchdog channel
 *
 * @param wdog_channel Watchdog channel to register
 * @param thread Thread responsible for feeding the channel
 */
void infuse_watchdog_thread_register(int wdog_channel, k_tid_t thread);

/**
 * @brief Determine state of the thread responsible for watchdog channel
 *
 * Data format, compatible with the Infuse Reboot API.
 *
 * info1:
 *     bits 16-31: Reserved for future use
 *     bits  8-15: Common thread state bits (_THREAD_PENDING, etc)
 *     bits  0- 7: Watchdog channel ID
 *
 * info2:
 *     If thread is pending on an object (_THREAD_PENDING), address of that object
 *     0 otherwise
 *
 * @param wdog_channel Watchdog channel to lookup thread state for
 * @param info1 Thread info per above
 * @param info2 Thread info per above
 *
 * @retval 0 on success
 * @retval -EINVAL @a wdog_channel has not been associated with a thread
 */
int infuse_watchdog_thread_state_lookup(int wdog_channel, uint32_t *info1, uint32_t *info2);

/**
 * @brief Start the Infuse watchdog
 *
 * @return value from @a wdt_setup
 */
int infuse_watchdog_start(void);

/**
 * @brief Feed an Infuse watchdog channel
 *
 * @param wdog_channel Channel from @ref infuse_watchdog_install
 */
void infuse_watchdog_feed(int wdog_channel);

/**
 * @brief Feed all Infuse watchdog channels
 *
 * @note Should only be used in situations where the action of one thread
 *       could impact the timing of all watchdog channels. One example of
 *       this is erasing internal flash on nRF SoCs.
 */
void infuse_watchdog_feed_all(void);

#else

#define INFUSE_WATCHDOG_REGISTER_SYS_INIT(name, dependency, chan_name, period_name)                \
	static k_timeout_t period_name = K_FOREVER;                                                \
	static int chan_name = 0

static inline int infuse_watchdog_install(k_timeout_t *feed_period)
{
	ARG_UNUSED(feed_period);

	return 0;
}

static inline void infuse_watchdog_thread_register(int wdog_channel, k_tid_t thread)
{
}

static inline int infuse_watchdog_thread_state_lookup(int wdog_channel, uint32_t *info1,
						      uint32_t *info2)
{
	ARG_UNUSED(wdog_channel);
	ARG_UNUSED(info1);
	ARG_UNUSED(info2);

	return -EINVAL;
}

static inline int infuse_watchdog_start(void)
{
	return 0;
}

static inline void infuse_watchdog_feed(int wdog_channel)
{
	ARG_UNUSED(wdog_channel);
}

static inline void infuse_watchdog_feed_all(void)
{
}

#endif /* defined(CONFIG_INFUSE_WATCHDOG) || defined(__DOXYGEN__) */

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_DRIVERS_WATCHDOG_H_ */
