/**
 * @file
 * @brief Bluetooth scanner task arguments
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_BT_SCANNER_ARGS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_BT_SCANNER_ARGS_H_

#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	TASK_BT_SCANNER_LOG_INFUSE_BT = BIT(0),
};

enum {
	/* Log packets that failed to decrypt */
	TASK_BT_SCANNER_FLAGS_LOG_ENCRYPTED = BIT(0),
	/* Only log each device once */
	TASK_BT_SCANNER_FLAGS_FILTER_DUPLICATES = BIT(1),
	/* Log observed devices at task termination */
	TASK_BT_SCANNER_FLAGS_DEFER_LOGGING = BIT(2),
};

/** @brief Bluetooth scanner task arguments */
struct task_bt_scanner_args {
	uint32_t duration_ms;
	uint8_t max_logs;
	uint8_t flags;
} __packed;

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_BT_SCANNER_ARGS_H_ */
