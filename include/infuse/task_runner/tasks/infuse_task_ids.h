/**
 * @file
 * @brief
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_INFUSE_TASK_IDS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_INFUSE_TASK_IDS_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief infuse_task_ids API
 * @defgroup infuse_task_ids_apis infuse_task_ids APIs
 * @{
 */

enum infuse_task_ids {
	TASK_ID_TDF_LOGGER = 0,
	TASK_ID_IMU = 1,
	TASK_ID_BATTERY = 2,
	TASK_ID_ENVIRONMENTAL = 3,
	TASK_ID_GNSS = 4,
	TASK_ID_NETWORK_SCAN = 5,
	TASK_ID_BT_SCANNER = 6,
	TASK_ID_TDF_LOGGER_ALT1 = 7,
	TASK_ID_TDF_LOGGER_ALT2 = 8,
};

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_INFUSE_TASK_IDS_H_ */
