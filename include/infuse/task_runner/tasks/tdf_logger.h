/**
 * @file
 * @brief TDF logger task
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
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
 * @param tdf_loggers TDF loggers to log to
 * @param timestamp Time to use for logging
 * @param tdfs TDFs to log (`TASK_TDF_LOGGER_LOG_*`)
 * @param custom_logger Custom logging function for @ref TASK_TDF_LOGGER_LOG_CUSTOM
 */
void task_tdf_logger_manual_run(uint8_t tdf_loggers, uint64_t timestamp, uint16_t tdfs,
				tdf_logger_custom_log_t custom_logger);

/**
 * @brief Common define for applications to re-use when multiple task instances are desired
 */
#define _TDF_LOGGER_TASK_INSTANCE(_name, _task_id, define_mem, define_config, custom_logger)       \
	IF_ENABLED(define_config, ({.name = _name,                                                 \
				    .task_id = _task_id,                                           \
				    .exec_type = TASK_EXECUTOR_WORKQUEUE,                          \
				    .task_arg.const_arg = custom_logger,                           \
				    .executor.workqueue = {                                        \
					    .worker_fn = task_tdf_logger_fn,                       \
				    }}))

/**
 * @brief Generic TDF logger task
 *
 * @param define_mem Define memory (None required)
 * @param define_config Define task
 * @param custom_logger Callback for custom logging
 * @param ... Compile-time argument unused
 */
#define TDF_LOGGER_TASK(define_mem, define_config, custom_logger)                                  \
	_TDF_LOGGER_TASK_INSTANCE("tdfl", TASK_ID_TDF_LOGGER, define_mem, define_config,           \
				  custom_logger)

/**
 * @brief TDF logger task, alternate instance 1
 *
 * Behaves the exact same way as @ref TDF_LOGGER_TASK, but with a different task ID.
 * This allows multiple instance of TDF logging to run concurrently with each other.
 *
 * @param define_mem Define memory (None required)
 * @param define_config Define task
 * @param custom_logger Callback for custom logging
 * @param ... Compile-time argument unused
 */
#define TDF_LOGGER_ALT1_TASK(define_mem, define_config, custom_logger)                             \
	_TDF_LOGGER_TASK_INSTANCE("tdfl1", TASK_ID_TDF_LOGGER_ALT1, define_mem, define_config,     \
				  custom_logger)

/**
 * @brief TDF logger task, alternate instance 2
 *
 * Behaves the exact same way as @ref TDF_LOGGER_TASK, but with a different task ID.
 * This allows multiple instance of TDF logging to run concurrently with each other.
 *
 * @param define_mem Define memory (None required)
 * @param define_config Define task
 * @param custom_logger Callback for custom logging
 * @param ... Compile-time argument unused
 */
#define TDF_LOGGER_ALT2_TASK(define_mem, define_config, custom_logger)                             \
	_TDF_LOGGER_TASK_INSTANCE("tdfl2", TASK_ID_TDF_LOGGER_ALT2, define_mem, define_config,     \
				  custom_logger)

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_TDF_LOGGER_H_ */
