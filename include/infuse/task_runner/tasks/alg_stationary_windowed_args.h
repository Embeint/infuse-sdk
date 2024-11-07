/**
 * @file
 * @brief Windowed stationary detection arguments
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_ALG_STATIONARY_WINDOWED_ARGS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_ALG_STATIONARY_WINDOWED_ARGS_H_

#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	TASK_ALG_STATIONARY_WINDOWED_LOG_WINDOW_STD_DEV = BIT(0),
};

/** @brief Algorithm stationary windowed task arguments */
struct task_alg_stationary_windowed_args {
	/** Duration of window to examine */
	uint32_t window_seconds;
	/* Standard deviation threshold in micro-g, above this value the device is moving */
	uint32_t std_dev_threshold_ug;
} __packed;

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_ALG_STATIONARY_WINDOWED_ARGS_H_ */
