/**
 * @file
 * @brief SoC temperature task arguments
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_SOC_TEMPERATURE_ARGS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_SOC_TEMPERATURE_ARGS_H_

#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	/* Temperature */
	TASK_SOC_TEMPERATURE_LOG_T = BIT(0),
};

/** @brief SoC temperature task arguments */
struct task_soc_temperature_args {
} __packed;

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_SOC_TEMPERATURE_ARGS_H_ */
