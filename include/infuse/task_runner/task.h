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
	union {
		struct {
			/** Thread function */
			task_runner_task_fn task_fn;
			/** Pointer to stack memory for thread */
			k_thread_stack_t *stack;
			/** Size of stack memory */
			size_t stack_size;
		} thread;
	} executor;
};

/**
 * @brief Task runtime state
 */
struct task_data {
	union {
		/** Thread state storage */
		struct k_thread thread;
	} executor;
	/** Thread termination signal */
	struct k_poll_signal terminate_signal;
	/** Schedule that triggered the task to run */
	uint8_t schedule_idx;
	/** Task is currently running */
	bool running;
};

/**
 * @brief Expand @a task_macro to define variable sized memory
 *
 * @param task_macro Macro that evaluates to a task variable memory
 *                   definition when the first argument is 1.
 */
#define _TASK_MEM_DEFINE(task_macro)    task_macro(1, 0)
/**
 * @brief Expand @a task_macro to define a task configuration
 *
 * @param task_macro Macro that evaluates to a task config definition
 *                   when the second argument is 1.
 */
#define _TASK_CONFIG_DEFINE(task_macro) task_macro(0, 1)

/* clang-format off */

/**
 * @brief Instantiate tasks information for task runner
 *
 * Helper macro that automatically creates the 3 items needed for correct
 * operation of the task runner:
 *    1. Instantiate variably sized memory for the task (e.g. thread stack)
 *    2. Array of task configuration structs for the runner
 *    3. Array of task data structs for the runner
 *
 * Example Usage:
 *
 * #define SLEEPY_TASK(define_mem, define_config)                               \
 *    IF_ENABLED(define_mem, (K_THREAD_STACK_DEFINE(sleep_stack_area, 1024)))   \
 *    IF_ENABLED(define_config,                                                 \
 *        ({                                                                    \
 *            .name = "sleepy",                                                 \
 *            .task_id = TASK_ID_SLEEPY,                                        \
 *            .executor.thread = {                                              \
 *                .task_fn = example_task_fn,                                   \
 *                .thread_stack = sleep_stack_area,                             \
 *                .thread_stack_size = K_THREAD_STACK_SIZEOF(sleep_stack_area), \
 *            },
 *        }))
 *
 * TASK_RUNNER_TASKS_DEFINE(config, data, SLEEPY_TASK);
 *
 * @param config_name Name of the created @ref task_config array
 * @param data_name Name of the created @ref task_data array
 * @param ... List of task definition macros to evaluate
 */
#define TASK_RUNNER_TASKS_DEFINE(config_name, data_name, ...)                                      \
	/* Define variable memory for the task */                                                  \
	FOR_EACH(_TASK_MEM_DEFINE, (;), __VA_ARGS__)                                               \
		;                                                                                  \
	/* Define the configurations for each task */                                              \
	static const struct task_config config_name[] = {                                          \
		FOR_EACH(_TASK_CONFIG_DEFINE, (,), __VA_ARGS__),                                   \
	};                                                                                         \
	/* Define the runtime task data array */                                                   \
	static struct task_data data_name[ARRAY_SIZE(config_name)]

/* clang-format on */

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
