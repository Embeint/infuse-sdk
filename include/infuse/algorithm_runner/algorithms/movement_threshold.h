/**
 * @file
 * @brief Movement detection based on static threshold with trailing window
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 * Only explicitly controls @a INFUSE_STATE_DEVICE_STOPPED_MOVING and
 * @a INFUSE_STATE_DEVICE_STARTED_MOVING, not the corresponding
 * @a INFUSE_STATE_DEVICE_STATIONARY and @a INFUSE_STATE_DEVICE_STOPPED_MOVING
 * states.
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_ALGORITHM_RUNNER_ALGORITHMS_MOVEMENT_THRESHOLD_H_
#define INFUSE_SDK_INCLUDE_INFUSE_ALGORITHM_RUNNER_ALGORITHMS_MOVEMENT_THRESHOLD_H_

#include <stdint.h>

#include <zephyr/toolchain.h>

#include <infuse/algorithm_runner/runner.h>
#include <infuse/zbus/channels.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Movement Threshold API
 * @defgroup movement_threshold_apis movement_threshold APIs
 * @{
 */

struct algorithm_movement_threshold_data {
	uint32_t threshold_low;
	uint32_t threshold_high;
	uint8_t full_scale_range;
};

/** Algorithm implementation, see @ref algorithm_run_fn */
void algorithm_movement_threshold_fn(const struct zbus_channel *chan,
				     const struct algorithm_runner_common_config *common,
				     const void *args, void *data);

/**
 * @brief Statically define an instance of the movement threshold algorithm
 *
 * @param name Variable name base
 * @param moving_for_ How long the moving state is set for
 * @param threshold_ug Magnitude this far away from 1G triggers the moving state
 */
#define ALGORITHM_MOVEMENT_THRESHOLD_DEFINE(name, moving_for_, threshold_ug_)                      \
	static const struct algorithm_runner_common_config name##_config = {                       \
		.algorithm_id = 0x15F20002,                                                        \
		.zbus_channel = INFUSE_ZBUS_CHAN_IMU_ACC_MAG,                                      \
		.arguments_size = sizeof(struct kv_alg_movement_threshold_args),                   \
		.state_size = sizeof(struct algorithm_movement_threshold_data),                    \
		.arguments_kv_key = KV_KEY_ALG_MOVEMENT_THRESHOLD_ARGS,                            \
	};                                                                                         \
	static struct kv_alg_movement_threshold_args name##_default_args = {                       \
		.logging = {0},                                                                    \
		.args =                                                                            \
			{                                                                          \
				.moving_for = moving_for_,                                         \
				.threshold_ug = threshold_ug_,                                     \
			},                                                                         \
	};                                                                                         \
	static struct algorithm_movement_threshold_data name##_data;                               \
	static struct algorithm_runner_algorithm name = {                                          \
		.impl = algorithm_movement_threshold_fn,                                           \
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

#endif /* INFUSE_SDK_INCLUDE_INFUSE_ALGORITHM_RUNNER_ALGORITHMS_MOVEMENT_THRESHOLD_H_ */
