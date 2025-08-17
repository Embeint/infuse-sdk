/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 * Algorithm that calculates the tilt of a device relative to a reference gravity vector.
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_ALGORITHM_RUNNER_ALGORITHMS_TILT_H_
#define INFUSE_SDK_INCLUDE_INFUSE_ALGORITHM_RUNNER_ALGORITHMS_TILT_H_

#include <stdint.h>

#include <zephyr/toolchain.h>

#include <infuse/algorithm_runner/runner.h>
#include <infuse/math/statistics.h>
#include <infuse/math/filter.h>
#include <infuse/fs/kv_types.h>
#include <infuse/zbus/channels.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief tilt API
 * @defgroup tilt_apis tilt APIs
 * @{
 */

enum {
	ALGORITHM_TILT_LOG_ANGLE = BIT(0),
};

struct algorithm_tilt_config {
	/** Common algorithm configuration */
	struct algorithm_runner_common_config common;
	/** IIR filter alpha (see @ref iir_filter_single_pole_f32) */
	float iir_filter_alpha;
	/** Percentage within one G magnitude must be to use for tilt calculation */
	uint8_t one_g_percent;
} __packed;

struct algorithm_tilt_data {
	struct iir_filter_single_pole_f32 filter;
	struct kv_gravity_reference gravity;
	uint32_t kv_store_crc;
	uint16_t gravity_mag;
	bool reference_valid;
};

/** Algorithm implementation, see @ref algorithm_run_fn */
void algorithm_tilt_fn(const struct zbus_channel *chan, const void *config, void *data);

/**
 * @brief Statically define an instance of the tilt algorithm
 *
 * @param name Variable name base
 * @param loggers_ TDF loggers to log outputs to
 * @param tdfs TDFs to log
 * @param filter_alpha IIR filter alpha parameter
 * @param one_g_valid_percent Accelerometer magnitude must be with N percent of 1G to use for tilt
 */
#define ALGORITHM_TILT_DEFINE(name, loggers_, tdfs, filter_alpha, one_g_valid_percent)             \
	static const struct algorithm_tilt_config name##_config = {                                \
		.common =                                                                          \
			{                                                                          \
				.algorithm_id = 0x15F20001,                                        \
				.zbus_channel = INFUSE_ZBUS_CHAN_IMU,                              \
				.state_size = sizeof(struct algorithm_tilt_data),                  \
				.logging =                                                         \
					{                                                          \
						.loggers = loggers_,                               \
						.tdf_mask = tdfs,                                  \
					},                                                         \
			},                                                                         \
		.iir_filter_alpha = filter_alpha,                                                  \
		.one_g_percent = one_g_valid_percent,                                              \
	};                                                                                         \
	static struct algorithm_tilt_data name##_data;                                             \
	static struct algorithm_runner_algorithm name = {                                          \
		.impl = algorithm_tilt_fn,                                                         \
		.config = &name##_config.common,                                                   \
		.runtime_state = &name##_data,                                                     \
	}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_ALGORITHM_RUNNER_ALGORITHMS_TILT_H_ */
