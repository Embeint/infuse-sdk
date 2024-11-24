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

#include <infuse/task_runner/tasks/infuse_task_ids.h>
#include <infuse/task_runner/tasks/tdf_logger.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Application callback for custom TDF logging
 *
 * @param tdf_loggers TDF loggers to log to
 * @param timestamp Time to use for logging
 */
typedef void (*tdf_logger_custom_log_t)(uint8_t tdf_loggers, uint64_t timestamp);

/**
 * @brief TDF logger runner function
 *
 * @param work Work object
 */
void task_tdf_logger_fn(struct k_work *work);

/**
 * @brief Manually run the core TDF logging logic
 *
 * @param args Arguments to use to run logging
 * @param custom_logger Custom logging function for @ref TASK_TDF_LOGGER_LOG_CUSTOM
 */
void task_tdf_logger_manual_run(const struct task_tdf_logger_args *args,
				tdf_logger_custom_log_t custom_logger);

/**
 * @brief TDF logger task
 *
 * @param define_mem Define memory (None required)
 * @param define_config Define task
 * @param custom_logger Callback for custom logging
 * @param ... Compile-time argument unused
 */
#define TDF_LOGGER_TASK(define_mem, define_config, custom_logger)                                  \
	IF_ENABLED(define_config, ({.name = "tdfl",                                                \
				    .task_id = TASK_ID_TDF_LOGGER,                                 \
				    .exec_type = TASK_EXECUTOR_WORKQUEUE,                          \
				    .task_arg.const_arg = custom_logger,                           \
				    .executor.workqueue = {                                        \
					    .worker_fn = task_tdf_logger_fn,                       \
				    }}))

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_TDF_LOGGER_H_ */
