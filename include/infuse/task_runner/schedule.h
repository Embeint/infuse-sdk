/**
 * @file
 * @brief Task Runner task scheduling
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_SCHEDULE_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_SCHEDULE_H_

#include <stdint.h>
#include <stdbool.h>

#include <zephyr/toolchain.h>
#include <zephyr/sys/atomic.h>

#include <infuse/task_runner/tasks/infuse_task_args.h>

#ifdef CONFIG_TASK_RUNNER_CUSTOM_TASK_DEFINITIONS
#include CONFIG_TASK_RUNNER_CUSTOM_TASK_DEFINITIONS_PATH
#endif /* CONFIG_TASK_RUNNER_CUSTOM_TASK_DEFINITIONS */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Task runner schedule API
 * @defgroup task_runner_schedule_apis Task runner schedule APIs
 * @{
 */

enum task_runner_valid_type {
	/** Do not update definition from KV store */
	TASK_LOCKED = 0x80,
	/** Task is always valid */
	TASK_VALID_ALWAYS = 1,
	/** Task is only valid when @ref INFUSE_STATE_APPLICATION_ACTIVE is set */
	TASK_VALID_ACTIVE = 2,
	/** Task is only valid when @ref INFUSE_STATE_APPLICATION_ACTIVE is not set */
	TASK_VALID_INACTIVE = 3,
	/** Entry and exit conditions never checked, task is rebooted if it terminates */
	TASK_VALID_PERMANENTLY_RUNS = 4,
	/** @cond INTERNAL_HIDDEN */
	_TASK_VALID_END,
	_TASK_VALID_MASK = 0x7F,
	/** @endcond */
};

enum task_runner_periodicity_type {
	/** Task can only run on N second boundaries */
	TASK_PERIODICITY_FIXED = 1,
	/** Task can only run N seconds after previous run started */
	TASK_PERIODICITY_LOCKOUT = 2,
	/** Task can only run N seconds after another schedule terminates */
	TASK_PERIODICITY_AFTER = 3,
	/** @ref TASK_PERIODICITY_LOCKOUT with a dynamic lockout based on battery charge */
	TASK_PERIODICITY_LOCKOUT_DYNAMIC_BATTERY = 4,
	/** @cond INTERNAL_HIDDEN */
	_TASK_PERIODICITY_END,
	/** @endcond */
};

/** Invert the state */
#define TR_NOT 0x100
/** OR the state with the previous result */
#define TR_OR  0x200
/** AND the state with the previous result */
#define TR_AND 0x00

/** @cond INTERNAL_HIDDEN */

#define _TR_STATE_BASE                 0xFF
#define _TR_NOT_BIT_COMMON(state, idx) ((state) & TR_NOT ? BIT(idx) : 0)
#define _TR_OR_BIT_COMMON(state, idx)  ((state) & TR_OR ? BIT(idx) : 0)
#define _TR_STATES_DEFINE_ALL(s0, s1, s2, s3)                                                      \
	{                                                                                          \
		.metadata = _TR_NOT_BIT_COMMON(s0, 0) | _TR_NOT_BIT_COMMON(s1, 1) |                \
			    _TR_NOT_BIT_COMMON(s2, 2) | _TR_NOT_BIT_COMMON(s3, 3) |                \
			    _TR_OR_BIT_COMMON(s0, 4) | _TR_OR_BIT_COMMON(s1, 5) |                  \
			    _TR_OR_BIT_COMMON(s2, 6) | _TR_OR_BIT_COMMON(s3, 7),                   \
		.states = {                                                                        \
			(s0) & _TR_STATE_BASE,                                                     \
			(s1) & _TR_STATE_BASE,                                                     \
			(s2) & _TR_STATE_BASE,                                                     \
			(s3) & _TR_STATE_BASE,                                                     \
		}                                                                                  \
	}

#define _TR_GET_MACRO(_1, _2, _3, _4, NAME, ...) NAME
#define _TR_STATES_1(arg1)                       _TR_STATES_DEFINE_ALL(arg1, 0, 0, 0)
#define _TR_STATES_2(arg1, arg2)                 _TR_STATES_DEFINE_ALL(arg1, arg2, 0, 0)
#define _TR_STATES_3(arg1, arg2, arg3)           _TR_STATES_DEFINE_ALL(arg1, arg2, arg3, 0)
#define _TR_STATES_4(arg1, arg2, arg3, arg4)     _TR_STATES_DEFINE_ALL(arg1, arg2, arg3, arg4)

/** @endcond */

/**
 * @brief Helper for constructing a task_schedule_state_conditions struct
 *
 * Each provided state can be optionally inverted (with @ref TR_NOT).
 * Each provided state is evaluated with the following priority:
 *    (((S0 OP S1) OP S2) OP S3)
 * By default, the OP between each state is AND (&&), but this can be switched to OR (||)
 * by specifying @ref TR_OR on S1, S2, or S3.
 *
 * @code{.c}
 * struct state_control test1 = TASK_STATES_DEFINE(10);
 * struct state_control test2 = TASK_STATES_DEFINE(10, 11, 45, 200);
 * struct state_control test3 = TASK_STATES_DEFINE(TR_NOT | 34, 12, TR_NOT | 99);
 *  struct state_control test4 = TASK_STATES_DEFINE(TR_NOT | 34, TR_OR | 12, TR_OR | TR_NOT | 99);
 * @endcode
 *
 * @param ... Variable number of states (up to 4) which are evaluated together.
 */
#define TASK_STATES_DEFINE(...)                                                                    \
	_TR_GET_MACRO(__VA_ARGS__, _TR_STATES_4, _TR_STATES_3, _TR_STATES_2, _TR_STATES_1)         \
	(__VA_ARGS__)

/**
 * @brief Helper macro to initialise `states_start_timeout_2x_s`
 *
 * Maximum value is `2 * UINT16_MAX`, just over 34 hours.
 *
 * @param seconds Desired timeout in seconds
 *
 * @retval Value for use with `states_start_timeout_2x_s`
 */
#define TASK_STATES_START_TIMEOUT(seconds) ((seconds) / 2)

/**
 * @brief Control TDF logging output of a task
 */
struct task_schedule_tdf_logging {
	/** TDF loggers to log to */
	uint8_t loggers;
	/** TDFs to log (bitmask defined by the activity) */
	uint8_t tdf_mask;
} __packed;

/**
 * @brief Schedule state conditions
 *
 * The result of the state conditional is ANDing all states together, with possible inversion
 * from the metadata field.
 */
struct task_schedule_state_conditions {
	/* Metadata associated with states (inversion) */
	uint8_t metadata;
	/** Array of states to test */
	uint8_t states[4];
} __packed;

/**
 * Normally the lockout period must elapse after boot before the periodicity check passes.
 * Apply to task_schedule.periodicity.lockout.lockout_s for the periodicity check to always
 * pass before the schedule runs for the first time.
 */
#define TASK_RUNNER_LOCKOUT_IGNORE_FIRST BIT(31)

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
	/** Task will not start for the first N minutes after boot */
	uint8_t boot_lockout_minutes;
	/** Duration after which task is requested to terminate */
	uint32_t timeout_s;
	/** Battery charge thresholds to start the task */
	struct battery_start_thresholds {
		/** Start task if >= this charge */
		uint8_t lower;
		/** Start task if <= this charge */
		uint8_t upper;
	} __packed battery_start;
	/** Battery charge thresholds to terminate the task */
	struct battery_terminate_thresholds {
		/** Terminate task if <= this charge */
		uint8_t lower;
		/** Terminate task if >= this charge */
		uint8_t upper;
	} __packed battery_terminate;
	/** Periodicity parameters */
	union periodicity_args {
		/** Start on global N second boundaries */
		struct periodicity_periodic {
			uint32_t period_s;
		} __packed fixed;
		/** Start N seconds after task last started */
		struct periodicity_lockout {
			uint32_t lockout_s;
		} __packed lockout;
		/** Start N seconds after another task finishes */
		struct periodicity_after {
			uint8_t schedule_idx;
			uint16_t duration_s;
		} __packed after;
		/**
		 * Lockout for a variable duration based on battery state.
		 * Lockout scales linearly between @a lockout_min and @a lockout_max
		 * when battery is between @a battery_min and @a battery_max.
		 */
		struct periodicity_lockout_dynamic_battery {
			/** Lockout between runs at <= @a battery_min */
			uint16_t lockout_min;
			/** Lockout between runs at >= @a battery_max */
			uint16_t lockout_max;
			/** Lower battery threshold */
			uint8_t battery_min;
			/** Upper battery threshold */
			uint8_t battery_max;
		} __packed lockout_dynamic_battery;
	} periodicity;
	/** @a states_start will evaluate as true 2x this many seconds after last run started */
	uint16_t states_start_timeout_2x_s;
	/** Task start state conditions */
	struct task_schedule_state_conditions states_start;
	/** Task termination state conditions */
	struct task_schedule_state_conditions states_terminate;
	/** Task logging configuration */
	struct task_schedule_tdf_logging task_logging[2];
	/** Task specific arguments  */
	union task_args {
		uint8_t raw[16];
		union infuse_task_arguments infuse;
#ifdef CONFIG_TASK_RUNNER_CUSTOM_TASK_DEFINITIONS
		union custom_task_arguments custom;
#endif /* CONFIG_TASK_RUNNER_CUSTOM_TASK_DEFINITIONS */
	} task_args;
} __packed;

/** Events that can trigger callbacks */
enum task_schedule_event {
	/** Task associated with the schedule has been started */
	TASK_SCHEDULE_STARTED = 0,
	/** Task associated with the schedule has been requested to terminate */
	TASK_SCHEDULE_TERMINATE_REQUEST = 1,
	/** Task associated with the schedule has stopped */
	TASK_SCHEDULE_STOPPED = 2,
};

/**
 * @brief Callback notifying that an event has occurred on a schedule
 *
 * @note Callback can only be assigned to the @ref task_schedule_state AFTER the
 *       call to @ref task_runner_init.
 *
 * @param schedule Schedule with the event
 * @param event Event that occurred
 */
typedef void (*task_schedule_event_cb_t)(const struct task_schedule *schedule,
					 enum task_schedule_event event);

/**
 * @brief State for a given task schedule
 *
 * One state struct exists per @ref task_schedule
 */
struct task_schedule_state {
	/** Linked schedule for @ref TASK_PERIODICITY_AFTER */
	struct task_schedule_state *linked;
	/** Optional callback to be notified of schedule events */
	task_schedule_event_cb_t event_cb;
	/** System uptime that started the last run of this schedule */
	uint32_t last_run;
	/** Duration of current run */
	uint32_t runtime;
	/** System uptime at termination of last run of this schedule */
	uint32_t last_terminate;
	/** Index into task array that corresponds with schedule @a task_id */
	uint8_t task_idx;
#ifdef CONFIG_TASK_RUNNER_PER_SCHEDULE_STORAGE
	/** Per schedule runtime state available for tasks to utilise */
	uint8_t runtime_state[CONFIG_TASK_RUNNER_PER_SCHEDULE_STORAGE];
#endif /* CONFIG_TASK_RUNNER_PER_SCHEDULE_STORAGE */
};

/** @cond INTERNAL_HIDDEN */
#define _TASK_SCHEDULE_MAX_SCHEDULES(schedules)                                                    \
	COND_CODE_1(CONFIG_KV_STORE_KEY_TASK_SCHEDULES,                                            \
		    (CONFIG_KV_STORE_KEY_TASK_SCHEDULES_RANGE), (ARRAY_SIZE(schedules)))
/** @endcond */

/**
 * @brief Instantiate schedule state array
 *
 * If loading schedules from the KV store is enabled, the array is sized to the maximum number
 * of schedules possible. Otherwise, it is sized to the number of default schedules.
 *
 * @param name Name of the schedule state array
 * @param schedules Array of default schedules
 */
#define TASK_SCHEDULE_STATES_DEFINE(name, schedules)                                               \
	static struct task_schedule_state name[_TASK_SCHEDULE_MAX_SCHEDULES(schedules)]

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
 * @param app_states Current application states
 * @param uptime Current system uptime in seconds
 * @param epoch_time Current epoch time (GPS time) in seconds
 * @param battery Battery charge percent
 *
 * @retval true Task should be started
 * @retval false Task should not be started
 */
bool task_schedule_should_start(const struct task_schedule *schedule,
				struct task_schedule_state *state, atomic_t *app_states,
				uint32_t uptime, uint32_t epoch_time, uint8_t battery);

/**
 * @brief Determine whether a task should be terminated
 *
 * @param schedule Task schedule to evaluate
 * @param state Previous state of the schedule
 * @param app_states Current application states
 * @param uptime Current system uptime in seconds
 * @param epoch_time Current epoch time (GPS time) in seconds
 * @param battery Battery charge percent
 *
 * @retval true Task should be terminated
 * @retval false Task should not be terminated
 */
bool task_schedule_should_terminate(const struct task_schedule *schedule,
				    struct task_schedule_state *state, atomic_t *app_states,
				    uint32_t uptime, uint32_t epoch_time, uint8_t battery);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_SCHEDULE_H_ */
