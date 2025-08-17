/**
 * @file
 * @brief Bluetooth scanner task
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_BT_SCANNER_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_BT_SCANNER_H_

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/schedule.h>

#include <infuse/task_runner/tasks/infuse_task_ids.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bluetooth scanner runner function
 *
 * @param work Work object
 */
void task_bt_scanner_fn(struct k_work *work);

/**
 * @brief Bluetooth scanner task
 *
 * @param define_mem Define memory (None required)
 * @param define_config Define task
 * @param ... Compile-time argument unused
 */
#define BT_SCANNER_TASK(define_mem, define_config, ...)                                            \
	IF_ENABLED(define_config, ({.name = "btsc",                                                \
				    .task_id = TASK_ID_BT_SCANNER,                                 \
				    .exec_type = TASK_EXECUTOR_WORKQUEUE,                          \
				    .executor.workqueue = {                                        \
					    .worker_fn = task_bt_scanner_fn,                       \
				    }}))

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_BT_SCANNER_H_ */
