/**
 * @file
 * @brief Stationary device detection using time windows
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_ALGORITHM_RUNNER_ALGORITHMS_STATIONARY_WINDOWED_H_
#define INFUSE_SDK_INCLUDE_INFUSE_ALGORITHM_RUNNER_ALGORITHMS_STATIONARY_WINDOWED_H_

#include <stdint.h>

#include <zephyr/toolchain.h>

#include <infuse/algorithm_runner/runner.h>
#include <infuse/math/statistics.h>
#include <infuse/zbus/channels.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief stationary_windowed API
 * @defgroup stationary_windowed_apis stationary_windowed APIs
 * @{
 */

enum {
	ALGORITHM_STATIONARY_WINDOWED_LOG_WINDOW_STD_DEV = BIT(0),
};

struct algorithm_stationary_windowed_config {
	/* Common algorithm configuration */
	struct algorithm_runner_common_config common;
	/** Duration of window to examine */
	uint32_t window_seconds;
	/* Standard deviation threshold in micro-g, above this value the device is moving */
	uint32_t std_dev_threshold_ug;
} __packed;

struct algorithm_stationary_windowed_data {
	struct statistics_state stats;
	uint32_t window_end;
	uint32_t print_end;
};

/** Algorithm implementation, see @ref algorithm_run_fn */
void algorithm_stationary_windowed_fn(const struct zbus_channel *chan, const void *config,
				      void *data);

/**
 * @brief Statically define an instance of the stationary windows algorithm
 *
 * @param name Variable name base
 * @param loggers_ TDF loggers to log outputs to
 * @param tdfs TDFs to log
 * @param window_seconds_ Duration of the time windows to examine in seconds
 * @param threshold_ug Standard deviation threshold in micro-g, above this value the device is
 *                     moving.
 */
#define ALGORITHM_STATIONARY_WINDOWED_DEFINE(name, loggers_, tdfs, window_seconds_, threshold_ug)  \
	static const struct algorithm_stationary_windowed_config name##_config = {                 \
		.common =                                                                          \
			{                                                                          \
				.algorithm_id = 0x15F20000,                                        \
				.zbus_channel = INFUSE_ZBUS_CHAN_IMU_ACC_MAG,                      \
				.state_size = sizeof(struct algorithm_stationary_windowed_data),   \
				.logging =                                                         \
					{                                                          \
						.loggers = loggers_,                               \
						.tdf_mask = tdfs,                                  \
					},                                                         \
			},                                                                         \
		.window_seconds = window_seconds_,                                                 \
		.std_dev_threshold_ug = threshold_ug,                                              \
	};                                                                                         \
	static struct algorithm_stationary_windowed_data name##_data;                              \
	static struct algorithm_runner_algorithm name = {                                          \
		.impl = algorithm_stationary_windowed_fn,                                          \
		.config = &name##_config.common,                                                   \
		.runtime_state = &name##_data,                                                     \
	}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_ALGORITHM_RUNNER_ALGORITHMS_STATIONARY_WINDOWED_H_ */
