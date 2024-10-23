/**
 * @file motion_id.h
 * @brief Motion identification task
 * @copyright 2024 Embeint Inc
 * @author Aeyohan Furtado <aeyohan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 *
 * @details
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_MOTION_ID_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_MOTION_ID_H_

#include <zephyr/kernel.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/schedule.h>

#include <infuse/task_runner/tasks/infuse_task_ids.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	MOTION_ID_DISABLED,
	MOTION_ID_INITIALISING,
	MOTION_ID_RUNNING,
};

/**
 * @brief Motion ID runner function
 *
 * @param work Work object
 */
void task_motion_id_fn(struct k_work *work);

#define MOTION_ID_TASK(define_mem, define_config, ...)                                             \
	IF_ENABLED(define_config, ({.name = "motion",                                              \
				    .task_id = TASK_ID_MOTION_ID,                                  \
				    .exec_type = TASK_EXECUTOR_WORKQUEUE,                          \
				    .executor.workqueue = {                                        \
					    .worker_fn = task_motion_id_fn,                        \
				    }}))

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_MOTION_ID_H_ */
