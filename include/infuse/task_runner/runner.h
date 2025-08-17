/**
 * @file
 * @brief Task Runner runner
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_RUNNER_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_RUNNER_H_

#include <stdint.h>

#include <zephyr/sys/atomic.h>
#include <zephyr/kernel.h>

#include <infuse/task_runner/schedule.h>
#include <infuse/task_runner/task.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Task Runner runner API
 * @defgroup task_runner_runner_apis Task Runner runner APIs
 * @{
 */

/**
 * @brief Initialise the task runner with a schedule list
 *
 * @param schedules Task schedules to evaluate
 * @param schedule_states State for each schedule in @a schedules
 * @param num_schedules Number of schedules and states
 * @param tasks Constant task configuration
 * @param task_states State for each task in @a task_config
 * @param num_tasks Number of tasks
 */
void task_runner_init(const struct task_schedule *schedules,
		      struct task_schedule_state *schedule_states, uint8_t num_schedules,
		      const struct task_config *tasks, struct task_data *task_states,
		      uint8_t num_tasks);

/**
 * @brief Iterate the task runner
 *
 * @warning MUST be called once a second
 *
 * @param app_states Application states
 * @param uptime Current device uptime in seconds
 * @param gps_time Current epoch time (GPS time) in seconds
 * @param battery_charge Battery charge percentage
 */
void task_runner_iterate(atomic_t *app_states, uint32_t uptime, uint32_t gps_time,
			 uint8_t battery_charge);

/**
 * @brief Automatically iterate the task runner
 *
 * Automatically calls @ref task_runner_iterate once a second forever.
 * Also calls @ref infuse_states_tick if CONFIG_INFUSE_APPLICATION_STATES is enabled.
 *
 * @warning Do NOT call @ref task_runner_iterate after this function.
 *
 * @retval Work item that performs the iteration
 */
struct k_work_delayable *task_runner_start_auto_iterate(void);

/**
 * @brief Get the watchdog channel associated with the task runner
 *
 * @return uint8_t Infuse-IoT watchdog channel
 */
uint8_t task_runner_watchdog_channel(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_RUNNER_H_ */
