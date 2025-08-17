/**
 * @file
 * @brief Network scan task
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_NETWORK_SCAN_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_NETWORK_SCAN_H_

#include <zephyr/kernel.h>

#include <infuse/tdf/definitions.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/schedule.h>

#include <infuse/task_runner/tasks/infuse_task_ids.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Network scan task function
 *
 * @param work Delayable work item
 */
void network_scan_task_fn(struct k_work *work);

/**
 * @brief Battery task
 *
 * @param define_mem Define memory
 * @param define_config Define task
 * @param _unused No argument required
 */
#define NETWORK_SCAN_TASK(define_mem, define_config, _unused)                                      \
	IF_ENABLED(define_config, ({.name = "nsc",                                                 \
				    .task_id = TASK_ID_NETWORK_SCAN,                               \
				    .exec_type = TASK_EXECUTOR_WORKQUEUE,                          \
				    .executor.workqueue = {                                        \
					    .worker_fn = network_scan_task_fn,                     \
				    }}))

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_NETWORK_SCAN_H_ */
