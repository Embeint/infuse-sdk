/**
 * @file
 * @brief Task Runner Task API
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASK_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASK_H_

#include <zephyr/kernel.h>

#include <infuse/task_runner/schedule.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief task API
 * @defgroup task_apis task APIs
 * @{
 */

typedef void (*task_runner_task_fn)(const struct task_schedule *schedule,
				    struct k_poll_signal *terminate);

/**
 * @brief Constant task configuration
 */
struct task_config {
	/** Task name */
	const char *name;
	/** Task identifier */
	uint8_t task_id;
	/** Thread function */
	task_runner_task_fn task_fn;
	/** Pointer to stack memory for thread */
	k_thread_stack_t *thread_stack;
	/** Size of stack memory */
	size_t thread_stack_size;
};

/**
 * @brief Task runtime state
 */
struct task_data {
	/** Thread state storage */
	struct k_thread thread;
	/** Thread termination signal */
	struct k_poll_signal terminate_signal;
	/** Schedule that triggered the task to run */
	uint8_t schedule_idx;
	/** Task is currently running */
	bool running;
};

/**
 * @brief Block on the termination signal for a duration
 *
 * @param terminate_signal Termination signal received from the runner
 * @param timeout Timeout for blocking
 *
 * @retval 1 If task should terminate
 * @retval 0 If task should continue execution
 */
static inline int task_runner_task_block(struct k_poll_signal *terminate_signal,
					 k_timeout_t timeout)
{
	struct k_poll_event events[1] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY,
					 terminate_signal),
	};
	int signaled, result;

	/* Block until requested timeout */
	(void)k_poll(events, 1, timeout);
	/* Determine if we have been requested to terminate */
	k_poll_signal_check(terminate_signal, &signaled, &result);
	return signaled ? 1 : 0;
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASK_H_ */
