/**
 * @file
 * @brief Task Runner task scheduling
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_SCHEDULE_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_SCHEDULE_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Task runner schedule API
 * @defgroup task_runner_schedule_apis Task runner schedule APIs
 * @{
 */

enum {
	TASK_VALID_ALWAYS = 0,
	TASK_VALID_ACTIVE = 1,
	TASK_VALID_INACTIVE = 2,
	_TASK_VALID_END,
};

enum {
	/** Task can only run on N second boundaries */
	TASK_PERIODICITY_FIXED = 1,
	/** Task can only run N seconds after previous run started */
	TASK_PERIODICITY_LOCKOUT = 2,
	_TASK_PERIODICITY_END,
};

/**
 * @brief Schedule for a given task
 *
 * Multiple schedules can exist for a single task.
 */
struct task_schedule {
	uint8_t task_id;
	/** TASK_VALID_* value */
	uint8_t validity;
	/** TASK_PERIODICITY_* value */
	uint8_t periodicity_type;
	uint8_t reserved1;
	/** Duration after which task is requested to terminate */
	uint32_t timeout_s;
	/** Task can start when battery is at least this charged */
	uint8_t battery_start_threshold;
	/** Task will terminate when battery falls to this level */
	uint8_t battery_terminate_threshold;
	uint16_t reserved2;
	union {
		struct {
			uint32_t period_s;
		} fixed;
		struct {
			uint32_t lockout_s;
		} lockout;
	} periodicity;
} __packed;

/**
 * @brief State for a given task schedule
 *
 * One state struct exists per @ref task_schedule
 */
struct task_schedule_state {
	/** System uptime that started the last run of this schedule */
	uint32_t last_run;
	/** Duration of current run */
	uint32_t runtime;
};

/**
 * @brief Basic validity checking on task schedules
 *
 * @note This function checks for schedules that would lead to divide-by-zero
 *       or similar errors, not for schedules that will never execute.
 *
 * @param schedule Schedule to validate
 *
 * @retval true If schedule doesn't contain invalid parameters
 * @retval false If schedule contains invalid parameters
 */
bool task_schedule_validate(const struct task_schedule *schedule);

/**
 * @brief Determine whether a task should start executing
 *
 * @param schedule Task schedule to evaluate
 * @param state Previous state of the schedule
 * @param uptime Current system uptime in seconds
 * @param civil_time Current civil time (GPS time) in seconds
 * @param battery Battery charge percent
 *
 * @retval true Task should be started
 * @retval false Task should not be started
 */
bool task_schedule_should_start(const struct task_schedule *schedule,
				struct task_schedule_state *state, uint32_t uptime,
				uint32_t civil_time, uint8_t battery);

/**
 * @brief Determine whether a task should be terminated
 *
 * @param schedule Task schedule to evaluate
 * @param state Previous state of the schedule
 * @param uptime Current system uptime in seconds
 * @param civil_time Current civil time (GPS time) in seconds
 * @param battery Battery charge percent
 *
 * @retval true Task should be terminated
 * @retval false Task should not be terminated
 */
bool task_schedule_should_terminate(const struct task_schedule *schedule,
				    struct task_schedule_state *state, uint32_t uptime,
				    uint32_t civil_time, uint8_t battery);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_SCHEDULE_H_ */
