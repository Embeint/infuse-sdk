/**
 * @file
 * @brief Battery measurement task
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_BATTERY_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_BATTERY_H_

#include <zephyr/kernel.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/schedule.h>

#include <infuse/task_runner/tasks/infuse_task_ids.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Battery task function
 *
 * @param work Delayable work item
 */
void battery_task_fn(struct k_work *work);

/**
 * @brief Battery task
 *
 * @param define_mem Define memory
 * @param define_config Define task
 * @param bat_ptr Battery device bound to task
 */
#define BATTERY_TASK(define_mem, define_config, bat_ptr)                                           \
	IF_ENABLED(define_config, ({.name = "bat",                                                 \
				    .task_id = TASK_ID_BATTERY,                                    \
				    .exec_type = TASK_EXECUTOR_WORKQUEUE,                          \
				    .task_arg.const_arg = bat_ptr,                                 \
				    .executor.workqueue = {                                        \
					    .worker_fn = battery_task_fn,                          \
				    }}))

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_BATTERY_H_ */
