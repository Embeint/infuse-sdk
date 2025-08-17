/**
 * @file
 * @brief GNSS task
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_GNSS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_GNSS_H_

#include <zephyr/kernel.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/schedule.h>

#include <infuse/task_runner/tasks/infuse_task_ids.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_TASK_RUNNER_TASK_GNSS_THREAD

/**
 * @brief GNSS task function
 *
 * @param schedule Schedule that triggered task
 * @param terminate Terminate request from task runner
 * @param gnss_dev GNSS device to use
 */
void gnss_task_fn(const struct task_schedule *schedule, struct k_poll_signal *terminate,
		  void *gnss_dev);

/**
 * @brief GNSS task
 *
 * @param define_mem Define memory
 * @param define_config Define task
 * @param gnss_ptr GNSS device bound to task
 */
#define GNSS_TASK(define_mem, define_config, gnss_ptr)                                             \
	IF_ENABLED(define_mem, (K_THREAD_STACK_DEFINE(gnss_stack_area,                             \
						      CONFIG_TASK_RUNNER_TASK_GNSS_STACK_SIZE);    \
				struct k_thread gnss_thread_obj))                                  \
	IF_ENABLED(define_config,                                                                  \
		   ({                                                                              \
			   .name = "gnss",                                                         \
			   .task_id = TASK_ID_GNSS,                                                \
			   .exec_type = TASK_EXECUTOR_THREAD,                                      \
			   .flags = TASK_FLAG_ARG_IS_DEVICE,                                       \
			   .task_arg.dev = gnss_ptr,                                               \
			   .executor.thread =                                                      \
				   {                                                               \
					   .thread = &gnss_thread_obj,                             \
					   .task_fn = gnss_task_fn,                                \
					   .stack = gnss_stack_area,                               \
					   .stack_size = K_THREAD_STACK_SIZEOF(gnss_stack_area),   \
				   },                                                              \
		   }))

#endif /* CONFIG_TASK_RUNNER_TASK_GNSS_THREAD */

#ifdef CONFIG_TASK_RUNNER_TASK_GNSS_WORKQUEUE

/**
 * @brief GNSS runner function
 *
 * @param work Work object
 */
void gnss_task_fn(struct k_work *work);

/**
 * @brief GNSS task
 *
 * @param define_mem Define memory
 * @param define_config Define task
 * @param gnss_ptr GNSS device bound to task
 */
#define GNSS_TASK(define_mem, define_config, gnss_ptr)                                             \
	IF_ENABLED(define_config, ({.name = "gnss",                                                \
				    .task_id = TASK_ID_GNSS,                                       \
				    .exec_type = TASK_EXECUTOR_WORKQUEUE,                          \
				    .flags = TASK_FLAG_ARG_IS_DEVICE,                              \
				    .task_arg.dev = gnss_ptr,                                      \
				    .executor.workqueue = {                                        \
					    .worker_fn = gnss_task_fn,                             \
				    }}))

#endif /* CONFIG_TASK_RUNNER_TASK_GNSS_WORKQUEUE */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_GNSS_H_ */
