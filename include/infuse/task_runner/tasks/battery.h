/**
 * @file
 * @brief Battery measurement task
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_BATTERY_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_BATTERY_H_

#include <zephyr/kernel.h>

#include <infuse/tdf/definitions.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/schedule.h>

#include <infuse/task_runner/tasks/infuse_task_ids.h>
#include <infuse/task_runner/tasks/battery.h>

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
 * @brief Manually run the core battery sampling logic
 *
 * @param dev Fuel gauge device
 * @param args Task arguments
 * @param tdf Battery parameters that were measured
 *
 * @retval 0 on success
 * @retval -errno on failure
 */
int task_battery_manual_run(const struct device *dev, const struct task_battery_args *args,
			    struct tdf_battery_state *tdf);

/**
 * @brief Battery task
 *
 * @param define_mem Define memory
 * @param define_config Define task
 * @param bat_ptr Fuel-gauge device bound to task
 */
#define BATTERY_TASK(define_mem, define_config, bat_ptr)                                           \
	IF_ENABLED(define_config, ({.name = "bat",                                                 \
				    .task_id = TASK_ID_BATTERY,                                    \
				    .exec_type = TASK_EXECUTOR_WORKQUEUE,                          \
				    .flags = TASK_FLAG_ARG_IS_DEVICE,                              \
				    .task_arg.dev = bat_ptr,                                       \
				    .executor.workqueue = {                                        \
					    .worker_fn = battery_task_fn,                          \
				    }}))

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_BATTERY_H_ */
