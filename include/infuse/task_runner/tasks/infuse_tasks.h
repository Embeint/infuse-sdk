/**
 * @file
 * @brief Task Runner Infuse-IoT tasks
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_INFUSE_TASKS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_INFUSE_TASKS_H_

#include <infuse/task_runner/tasks/tdf_logger.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief infuse_tasks API
 * @defgroup infuse_tasks_apis infuse_tasks APIs
 * @{
 */

enum infuse_task_ids {
	TASK_ID_TDF_LOGGER = 0,
};

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_INFUSE_TASKS_H_ */
