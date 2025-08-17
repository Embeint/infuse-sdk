/**
 * @file
 * @brief Demonstration of `TASK_RUNNER_CUSTOM_TASK_DEFINITIONS_PATH`
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_TESTS_SUBSYS_TASK_RUNNER_SCHEDULES_INCLUDE_CUSTOM_TASKS_H_
#define INFUSE_SDK_TESTS_SUBSYS_TASK_RUNNER_SCHEDULES_INCLUDE_CUSTOM_TASKS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct custom_args_1 {
	uint32_t arg1;
	int16_t arg2;
	uint8_t arg3[5];
} __packed;

struct custom_args_2 {
	uint8_t arg1;
	int32_t arg2;
	uint16_t arg3[5];
} __packed;

union custom_task_arguments {
	struct custom_args_1 custom1;
	struct custom_args_2 custom2;
} __packed;

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_TESTS_SUBSYS_TASK_RUNNER_SCHEDULES_INCLUDE_CUSTOM_TASKS_H_ */
