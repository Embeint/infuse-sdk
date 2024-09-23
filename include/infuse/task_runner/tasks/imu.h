/**
 * @file
 * @brief IMU task
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_IMU_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_IMU_H_

#include <zephyr/kernel.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/schedule.h>

#include <infuse/task_runner/tasks/infuse_task_ids.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief IMU task function
 *
 * @param schedule Schedule that triggered task
 * @param terminate Terminate request from task runner
 * @param imu_dev IMU device to use
 */
void imu_task_fn(const struct task_schedule *schedule, struct k_poll_signal *terminate,
		 void *imu_dev);

/**
 * @brief IMU task
 *
 * @param define_mem Define memory
 * @param define_config Define task
 * @param imu_ptr IMU device bound to task
 */
#define IMU_TASK(define_mem, define_config, imu_ptr)                                               \
	IF_ENABLED(define_mem, (K_THREAD_STACK_DEFINE(imu_stack_area, 2048)))                      \
	IF_ENABLED(define_config,                                                                  \
		   ({                                                                              \
			   .name = "imu",                                                          \
			   .task_id = TASK_ID_IMU,                                                 \
			   .exec_type = TASK_EXECUTOR_THREAD,                                      \
			   .flags = TASK_FLAG_ARG_IS_DEVICE,                                       \
			   .task_arg.dev = imu_ptr,                                                \
			   .executor.thread =                                                      \
				   {                                                               \
					   .task_fn = imu_task_fn,                                 \
					   .stack = imu_stack_area,                                \
					   .stack_size = K_THREAD_STACK_SIZEOF(imu_stack_area),    \
				   },                                                              \
		   }))

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_IMU_H_ */
