/**
 * @file
 * @brief TDF logger task
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_TDF_LOGGER_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_TDF_LOGGER_H_

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/schedule.h>

#include <infuse/task_runner/tasks/infuse_tasks.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief TDF logger runner function
 *
 * @param work Work object
 */
void task_tdf_logger_fn(struct k_work *work);

/**
 * @brief TDF logger task
 *
 * @param define_mem Define memory (None required)
 * @param define_config Define task
 */
#define TDF_LOGGER_TASK(define_mem, define_config)                                                 \
	IF_ENABLED(define_config, ({.name = "tdfl",                                                \
				    .task_id = TASK_ID_TDF_LOGGER,                                 \
				    .exec_type = TASK_EXECUTOR_WORKQUEUE,                          \
				    .executor.workqueue = {                                        \
					    .worker_fn = task_tdf_logger_fn,                       \
				    }}))

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_TDF_LOGGER_H_ */
