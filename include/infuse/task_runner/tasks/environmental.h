/**
 * @file
 * @brief Environmental sensing task
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_ENVIRONMENTAL_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_ENVIRONMENTAL_H_

#include <zephyr/kernel.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/schedule.h>

#include <infuse/task_runner/tasks/infuse_task_ids.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Environmental task function
 *
 * @param work Delayable work item
 */
void environmental_task_fn(struct k_work *work);

/**
 * @brief Environmental task
 *
 * @param define_mem Define memory
 * @param define_config Define task
 * @param env_ptr Environmental sensing device bound to task
 */
#define ENVIRONMENTAL_TASK(define_mem, define_config, env_ptr)                                     \
	IF_ENABLED(define_config, ({.name = "env",                                                 \
				    .task_id = TASK_ID_ENVIRONMENTAL,                              \
				    .exec_type = TASK_EXECUTOR_WORKQUEUE,                          \
				    .flags = TASK_FLAG_ARG_IS_DEVICE,                              \
				    .task_arg.dev = env_ptr,                                       \
				    .executor.workqueue = {                                        \
					    .worker_fn = environmental_task_fn,                    \
				    }}))

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_ENVIRONMENTAL_H_ */
