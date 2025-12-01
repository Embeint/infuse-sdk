/**
 * @file
 * @brief Demonstration algorithms
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_ALGORITHM_RUNNER_ALGORITHMS_DEMO_H_
#define INFUSE_SDK_INCLUDE_INFUSE_ALGORITHM_RUNNER_ALGORITHMS_DEMO_H_

#include <stdint.h>

#include <zephyr/toolchain.h>

#include <infuse/algorithm_runner/runner.h>
#include <infuse/zbus/channels.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief demo API
 * @defgroup demo_apis demo APIs
 * @{
 */

enum {
	ALGORITHM_DEMO_EVENT_LOG = BIT(0),
	ALGORITHM_DEMO_STATE_LOG = BIT(0),
	ALGORITHM_DEMO_METRIC_LOG = BIT(0),
};

struct algorithm_demo_common_args {
	/* Common logging configuration */
	struct kv_algorithm_logging logging;
	/* Algorithm specific arguments */
	union {
		/* Chance to emit an event on each buffer (%) */
		uint8_t event_gen_chance;
		/* Number of samples to compute metric over */
		uint16_t compute_metric_len;
	} __packed;
} __packed;

union algorithm_demo_common_data {
	uint32_t processed;
	uint8_t current_state;
};

/** Algorithm implementation, see @ref algorithm_run_fn */
void algorithm_demo_event_fn(const struct zbus_channel *chan,
			     const struct algorithm_runner_common_config *common, const void *args,
			     void *data);

/** Algorithm implementation, see @ref algorithm_run_fn */
void algorithm_demo_state_fn(const struct zbus_channel *chan,
			     const struct algorithm_runner_common_config *common, const void *args,
			     void *data);

/** Algorithm implementation, see @ref algorithm_run_fn */
void algorithm_demo_metric_fn(const struct zbus_channel *chan,
			      const struct algorithm_runner_common_config *common, const void *args,
			      void *data);

/**
 * @brief Statically define an instance of the demo event algorithm
 *
 * This algorithm randomly generates events, with a percentage chance equal to the argument on
 * each IMU sample buffer that arrives.
 *
 * @param name Variable name base
 * @param loggers_ TDF loggers to log outputs to
 * @param tdfs TDFs to log
 * @param event_chance_percent Chance to emit an event on each buffer
 */
#define ALGORITHM_DEMO_EVENT_DEFINE(name, loggers_, tdfs, event_chance_percent)                    \
	static const struct algorithm_runner_common_config name##_config = {                       \
		.algorithm_id = 0xFFFFFFF0,                                                        \
		.zbus_channel = INFUSE_ZBUS_CHAN_IMU,                                              \
		.arguments_size = sizeof(struct algorithm_demo_common_args),                       \
		.state_size = sizeof(union algorithm_demo_common_data),                            \
	};                                                                                         \
	static struct algorithm_demo_common_args name##_default_args = {                           \
		.logging =                                                                         \
			{                                                                          \
				.loggers = loggers_,                                               \
				.tdf_mask = tdfs,                                                  \
			},                                                                         \
		.event_gen_chance = event_chance_percent,                                          \
	};                                                                                         \
	static union algorithm_demo_common_data name##_data;                                       \
	static struct algorithm_runner_algorithm name = {                                          \
		.impl = algorithm_demo_event_fn,                                                   \
		.config = &name##_config,                                                          \
		.arguments = &name##_default_args,                                                 \
		.runtime_state = &name##_data,                                                     \
	}

/**
 * @brief Statically define an instance of the demo state algorithm
 *
 * This algorithm randomly transitions between states on IMU buffers
 *
 * @param name Variable name base
 * @param loggers_ TDF loggers to log outputs to
 * @param tdfs TDFs to log
 */
#define ALGORITHM_DEMO_STATE_DEFINE(name, loggers_, tdfs)                                          \
	static const struct algorithm_runner_common_config name##_config = {                       \
		.algorithm_id = 0xFFFFFFF1,                                                        \
		.zbus_channel = INFUSE_ZBUS_CHAN_IMU,                                              \
		.arguments_size = sizeof(struct algorithm_demo_common_args),                       \
		.state_size = sizeof(union algorithm_demo_common_data),                            \
	};                                                                                         \
	static struct algorithm_demo_common_args name##_default_args = {                           \
		.logging =                                                                         \
			{                                                                          \
				.loggers = loggers_,                                               \
				.tdf_mask = tdfs,                                                  \
			},                                                                         \
	};                                                                                         \
	static union algorithm_demo_common_data name##_data;                                       \
	static struct algorithm_runner_algorithm name = {                                          \
		.impl = algorithm_demo_state_fn,                                                   \
		.config = &name##_config,                                                          \
		.arguments = &name##_default_args,                                                 \
		.runtime_state = &name##_data,                                                     \
	}

/**
 * @brief Statically define an instance of the demo compute algorithm
 *
 * This algorithm generates a compute metric, with one metric computed every N samples
 *
 * @param name Variable name base
 * @param loggers_ TDF loggers to log outputs to
 * @param tdfs TDFs to log
 * @param metric_compute_len Number of samples to compute metric over
 */
#define ALGORITHM_DEMO_METRIC_DEFINE(name, loggers_, tdfs, metric_compute_len)                     \
	static const struct algorithm_runner_common_config name##_config = {                       \
		.algorithm_id = 0xFFFFFFF2,                                                        \
		.zbus_channel = INFUSE_ZBUS_CHAN_IMU,                                              \
		.arguments_size = sizeof(struct algorithm_demo_common_args),                       \
		.state_size = sizeof(union algorithm_demo_common_data),                            \
	};                                                                                         \
	static struct algorithm_demo_common_args name##_default_args = {                           \
		.logging =                                                                         \
			{                                                                          \
				.loggers = loggers_,                                               \
				.tdf_mask = tdfs,                                                  \
			},                                                                         \
		.compute_metric_len = metric_compute_len,                                          \
	};                                                                                         \
	static union algorithm_demo_common_data name##_data;                                       \
	static struct algorithm_runner_algorithm name = {                                          \
		.impl = algorithm_demo_metric_fn,                                                  \
		.config = &name##_config,                                                          \
		.arguments = &name##_default_args,                                                 \
		.runtime_state = &name##_data,                                                     \
	}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_ALGORITHM_RUNNER_ALGORITHMS_DEMO_H_ */
