/**
 * @file
 * @brief Task Runner Infuse-IoT task arguments
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_INFUSE_TASK_ARGS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_INFUSE_TASK_ARGS_H_

#include <infuse/task_runner/tasks/alg_stationary_windowed_args.h>
#include <infuse/task_runner/tasks/battery_args.h>
#include <infuse/task_runner/tasks/environmental_args.h>
#include <infuse/task_runner/tasks/gnss_args.h>
#include <infuse/task_runner/tasks/tdf_logger_args.h>
#include <infuse/task_runner/tasks/imu_args.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief infuse_task_args API
 * @defgroup infuse_task_args_apis infuse_task_args APIs
 * @{
 */

union infuse_task_arguments {
	struct task_alg_stationary_windowed_args alg_stationary_windowed;
	struct task_tdf_logger_args tdf_logger;
	struct task_imu_args imu;
	struct task_battery_args battery;
	struct task_environmental_args environmental;
	struct task_gnss_args gnss;
};

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_INFUSE_TASK_ARGS_H_ */
