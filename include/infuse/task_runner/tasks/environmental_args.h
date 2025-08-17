/**
 * @file
 * @brief Environmental task arguments
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_ENVIRONMENTAL_ARGS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_ENVIRONMENTAL_ARGS_H_

#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	/* Temperature, Pressure, Humidity */
	TASK_ENVIRONMENTAL_LOG_TPH = BIT(0),
	/* Temperature */
	TASK_ENVIRONMENTAL_LOG_T = BIT(1),
};

/** @brief Environmental task arguments */
struct task_environmental_args {
} __packed;

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_ENVIRONMENTAL_ARGS_H_ */
