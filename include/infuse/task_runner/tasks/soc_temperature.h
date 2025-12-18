/**
 * @file
 * @brief SoC temperature sensing task
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_SOC_TEMPERATURE_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_SOC_TEMPERATURE_H_

#include <zephyr/kernel.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/schedule.h>

#include <infuse/task_runner/tasks/infuse_task_ids.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief SoC temperature sensing task function
 *
 * @param work Delayable work item
 */
void soc_temperature_task_fn(struct k_work *work);

/**
 * @brief SoC temperature sensing task
 *
 * @param define_mem Define memory
 * @param define_config Define task
 * @param soc_temp_dev SoC temperature sensing device
 */
#define SOC_TEMPERATURE_TASK(define_mem, define_config, soc_temp_dev)                              \
	IF_ENABLED(define_config, ({.name = "soc_temp",                                            \
				    .task_id = TASK_ID_SOC_TEMPERATURE,                            \
				    .exec_type = TASK_EXECUTOR_WORKQUEUE,                          \
				    .flags = TASK_FLAG_ARG_IS_DEVICE,                              \
				    .task_arg.dev = soc_temp_dev,                                  \
				    .executor.workqueue = {                                        \
					    .worker_fn = soc_temperature_task_fn,                  \
				    }}))

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_SOC_TEMPERATURE_H_ */
