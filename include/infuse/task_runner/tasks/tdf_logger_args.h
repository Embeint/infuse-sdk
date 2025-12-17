/**
 * @file
 * @brief TDF logger task arguments
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
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
	TASK_TDF_LOGGER_LOG_BATTERY = BIT(1),
	TASK_TDF_LOGGER_LOG_AMBIENT_ENV = BIT(2),
	TASK_TDF_LOGGER_LOG_LOCATION = BIT(3),
	TASK_TDF_LOGGER_LOG_ACCEL = BIT(4),
	TASK_TDF_LOGGER_LOG_NET_CONN = BIT(5),
	TASK_TDF_LOGGER_LOG_CUSTOM = BIT(6),
	TASK_TDF_LOGGER_LOG_SOC_TEMPERATURE = BIT(7),
};

enum {
	/** Don't flush logger after logging (Logs with timestamp) */
	TASK_TDF_LOGGER_FLAGS_NO_FLUSH = BIT(0),
};

/** @brief TDF logger task arguments
 *
 * When @a logging_period_ms is set, the reschedule period is equal to
 * @a logging_period_ms plus the random delay from @a random_delay_ms
 */
struct task_tdf_logger_args {
	/** Mask of `TDF_DATA_LOGGER_*` to log to */
	uint8_t loggers;
	/* Reschedule next log in this many milliseconds */
	uint16_t logging_period_ms;
	/* Randomise delay before logging */
	uint16_t random_delay_ms;
	/* TDFs to log */
	uint16_t tdfs;
	/* Operation flags */
	uint8_t flags;
	/* Only log this many TDFs per run */
	uint8_t per_run;
} __packed;

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_TDF_LOGGER_ARGS_H_ */
