/**
 * @file
 * @brief Windowed stationary detection task
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_ALG_STATIONARY_WINDOWED_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_ALG_STATIONARY_WINDOWED_H_

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/schedule.h>

#include <infuse/task_runner/tasks/infuse_task_ids.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Windowed stationary detection runner function
 *
 * @param work Work object
 */
void task_alg_stationary_windowed_fn(struct k_work *work);

/**
 * @brief Windowed stationary detection task
 *
 * @param define_mem Define memory (None required)
 * @param define_config Define task
 * @param ... Compile-time argument unused
 */
#define ALG_STATIONARY_WINDOWED_TASK(define_mem, define_config, ...)                               \
	IF_ENABLED(define_config, ({.name = "asw",                                                 \
				    .task_id = TASK_ID_ALG_STATIONARY,                             \
				    .exec_type = TASK_EXECUTOR_WORKQUEUE,                          \
				    .executor.workqueue = {                                        \
					    .worker_fn = task_alg_stationary_windowed_fn,          \
				    }}))

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_ALG_STATIONARY_WINDOWED_H_ */
