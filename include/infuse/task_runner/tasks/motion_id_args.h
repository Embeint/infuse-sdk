/**
 * @file motion_id_args.h
 * @brief Motion identification task arguments
 * @copyright 2024 Embeint Inc
 * @author Aeyohan Furtado <aeyohan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 *
 * @details
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_MOTION_ID_ARGS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_MOTION_ID_ARGS_H_

#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

struct task_motion_id_args {
	/* Instantaneous acceleration threshold for movement in milli G's. */
	uint16_t threshold_millig;
	/* Once moving, how long should movement last for in task ticks. */
	uint8_t in_motion_timeout;
} __packed;

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_MOTION_ID_ARGS_H_ */
