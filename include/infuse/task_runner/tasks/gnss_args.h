/**
 * @file
 * @brief GNSS task arguments
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_GNSS_ARGS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_GNSS_ARGS_H_

#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	TASK_GNSS_LOG_LLHA = BIT(0),
	/* UBX modems only */
	TASK_GNSS_LOG_UBX_NAV_PVT = BIT(1),
};

enum {
	/** Bits 1-0: Run until */
	TASK_GNSS_FLAGS_RUN_FOREVER = 0,
	TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX = 1,
	TASK_GNSS_FLAGS_RUN_TO_TIME_SYNC = 2,
	TASK_GNSS_FLAGS_RUN_MASK = 0x3,
	/** Bit 7: Performance mode */
	TASK_GNSS_FLAGS_PERFORMANCE_MODE = BIT(7),
	TASK_GNSS_FLAGS_LOW_POWER_MODE = 0x00,
};

/** @brief GNSS task arguments */
struct task_gnss_args {
	/** Operational flags */
	uint8_t flags;
	/**
	 * Horizontal accuracy (meters)
	 * In @a TASK_GNSS_FLAGS_LOW_POWER_MODE, sets desired accuracy.
	 * In @a TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX, sets accuracy to terminate at.
	 */
	uint16_t accuracy_m;
	/**
	 * Horizontal diluation of precision (0.1)
	 * In @a TASK_GNSS_FLAGS_LOW_POWER_MODE, sets desired PDOP.
	 * In @a TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX, sets PDOP to terminate at.
	 */
	uint16_t position_dop;
	/** @a TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX specific arguments */
	struct {
		/** Terminate fix if this duration passes without any location information */
		uint8_t any_fix_timeout;
		/** Terminate fix if the accuracy plateaus */
		struct {
			/** Location accuracy must improve by at least this many meters */
			uint8_t min_accuracy_improvement;
			/** Timeout for accuracy to improve by @a min_accuracy_improvement */
			uint8_t timeout;
		} fix_plateau;
	} run_to_fix;
} __packed;

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_GNSS_ARGS_H_ */
