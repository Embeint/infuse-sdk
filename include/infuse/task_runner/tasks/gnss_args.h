/**
 * @file
 * @brief GNSS task arguments
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_GNSS_ARGS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_GNSS_ARGS_H_

#include <stdint.h>

#include <zephyr/sys/util.h>

/* UBLOX definitions are used since they are more specific than generic Zephyr */
#include <infuse/gnss/ubx/cfg.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	TASK_GNSS_LOG_LLHA = BIT(0),
	TASK_GNSS_LOG_FIX_INFO = BIT(1),
	/* Maximum information Position-Velocity-Time */
	TASK_GNSS_LOG_PVT = BIT(7),
};

enum {
	/** Runs until terminated by the scheduler */
	TASK_GNSS_FLAGS_RUN_FOREVER = 0,
	/** Terminates when the location is known to specified accuracy, implies performance mode */
	TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX = 1,
	/** Terminates when the time has been synced, implies performance mode */
	TASK_GNSS_FLAGS_RUN_TO_TIME_SYNC = 2,
	/** Bits 1-0: Run until */
	TASK_GNSS_FLAGS_RUN_MASK = 0x3,
	/** Bit 7: Performance mode */
	TASK_GNSS_FLAGS_PERFORMANCE_MODE = BIT(7),
	/**
	 * Note that the configured accuracy and position values for this mode do not equate to
	 * targeted or expected accuracies. Instead they specify thresholds for checking whether
	 * a fix has been obtained or if the modem should stop and try again later.
	 */
	TASK_GNSS_FLAGS_LOW_POWER_MODE = 0x00,
};

/** @brief GNSS task arguments */
struct task_gnss_args {
	/** Constellations `GNSS_SYSTEM_*` (0 == receiver default) */
	uint8_t constellations;
	/** Operational flags */
	uint8_t flags;
	/**
	 * Accuracy (meters)
	 *
	 * For Microntroller based checks (@a TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX) this is the
	 * horizontal accuracy. For GNSS modem implemented functionality, this may be 3D accuracy.
	 *
	 * In @a TASK_GNSS_FLAGS_LOW_POWER_MODE, sets accuracy to transition to low power mode.
	 * In @a TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX, sets accuracy to terminate at.
	 */
	uint16_t accuracy_m;
	/**
	 * Diluation of precision (0.1)
	 *
	 * For Microntroller based checks (@a TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX) this is the
	 * horizontal DOP. For GNSS modem implemented functionality, this may be 3D DOP.
	 *
	 * In @a TASK_GNSS_FLAGS_LOW_POWER_MODE, sets accuracy to transition to low power mode.
	 * In @a TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX, sets PDOP to terminate at.
	 */
	uint16_t position_dop;
	union {
		/** @a TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX specific arguments */
		struct {
			/** Terminate fix if this duration passes without any location information
			 */
			uint8_t any_fix_timeout;
			/** Terminate fix if the accuracy plateaus */
			struct task_gnss_plateau_args {
				/** Plateau detection only enabled once accuracy reaches this level
				 */
				uint8_t min_accuracy_m;
				/** Location accuracy must improve by at least this many meters */
				uint8_t min_accuracy_improvement_m;
				/** Timeout for accuracy to improve by @a min_accuracy_improvement
				 */
				uint8_t timeout;
			} fix_plateau;
		} run_to_fix;
	};
	/** Dynamic model from @ref ubx_cfg_key_navspg_dynmodel */
	uint8_t dynamic_model;
} __packed;

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_GNSS_ARGS_H_ */
