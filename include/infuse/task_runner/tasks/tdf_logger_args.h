/**
 * @file
 * @brief TDF logger task arguments
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_TDF_LOGGER_ARGS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_TDF_LOGGER_ARGS_H_

#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	TASK_TDF_LOGGER_LOG_ANNOUNCE = BIT(0),
};

/** @brief TDF logger task arguments */
struct task_tdf_logger_args {
	/** `TDF_DATA_LOGGER_*` to log to */
	uint8_t logger;
	/* Randomise delay before logging */
	uint16_t random_delay_ms;
	/* TDFs to log */
	uint16_t tdfs;
} __packed;

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_TDF_LOGGER_ARGS_H_ */
