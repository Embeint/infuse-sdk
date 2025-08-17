/**
 * @file
 * @brief Battery task arguments
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_BATTERY_ARGS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_BATTERY_ARGS_H_

#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	/* Battery voltage, charge current, charge percentage */
	TASK_BATTERY_LOG_COMPLETE = BIT(0),
};

/** @brief Battery task arguments */
struct task_battery_args {
	/* Period between measurements (0 for single shot mode) */
	uint16_t repeat_interval_ms;
} __packed;

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_BATTERY_ARGS_H_ */
