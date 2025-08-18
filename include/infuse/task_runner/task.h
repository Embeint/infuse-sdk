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
#include <zephyr/device.h>

#include <infuse/task_runner/schedule.h>
#include <infuse/data_logger/high_level/tdf.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief task API
 * @defgroup task_apis task APIs
 * @{
 */

enum {
	/** Task runs on its own thread */
	TASK_EXECUTOR_THREAD,
	/** Task runs on the system workqueue */
	TASK_EXECUTOR_WORKQUEUE,
};

enum {
	/** task_arg union is a device pointer */
	TASK_FLAG_ARG_IS_DEVICE = BIT(0),
};

typedef void (*task_runner_task_fn)(const struct task_schedule *schedule,
				    struct k_poll_signal *terminate, void *arg);

/**
 * @brief Constant task configuration
 */
struct task_config {
	/** Task name */
	const char *name;
	/** Task identifier */
	uint8_t task_id;
	/** Execution context `TASK_EXECUTOR_*` */
	uint8_t exec_type;
	/** Task flags of type `TASK_FLAG_*` */
	uint8_t flags;
	/* Task specific argument */
	union {
		const struct device *dev;
		const void *const_arg;
		void *arg;
	} task_arg;
	union {
		struct {
			/** Thread state storage */
			struct k_thread *thread;
			/** Thread function */
			task_runner_task_fn task_fn;
			/** Pointer to stack memory for thread */
			k_thread_stack_t *stack;
			/** Size of stack memory */
			size_t stack_size;
		} thread;
		struct {
			/** Handler function */
			k_work_handler_t worker_fn;
			/** Persistent state */
			void *state;
		} workqueue;
	} executor;
};

/**
 * @brief Task runtime state
 */
struct task_data {
	union {
		/** Workqueue state storage */
		struct {
			/* Workqueue item */
			struct k_work_delayable work;
			/* Counter for the number of times the work has been rescheduled this run */
			int reschedule_counter;
			/* Compile-time argument */
			union {
				const void *const_arg;
				void *arg;
			} task_arg;
		} workqueue;
	} executor;
	/** Thread termination signal */
	struct k_poll_signal terminate_signal;
	/** Schedule that triggered the task to run */
	uint8_t schedule_idx;
	/** Task is currently running */
	bool running;
	/** Skip evaluation of task */
	bool skip;
};

/**
 * @brief Expand @a macro to define task specific variables
 *
 * @param macro Macro that instantiates task specific variables
 *              when the first argument is 1.
 */
#define ___TASK_VAR_DEFINE(macro, ...)    macro(1, 0, __VA_ARGS__)
#define __TASK_VAR_DEFINE(...)            ___TASK_VAR_DEFINE(__VA_ARGS__)
#define _TASK_VAR_DEFINE(task)            __TASK_VAR_DEFINE(__DEBRACKET task)
/**
 * @brief Expand @a macro to define a task configuration
 *
 * @param macro Macro that evaluates to a task config struct definition
 *              when the second argument is 1.
 */
#define ___TASK_CONFIG_DEFINE(macro, ...) macro(0, 1, __VA_ARGS__)
#define __TASK_CONFIG_DEFINE(...)         ___TASK_CONFIG_DEFINE(__VA_ARGS__)
#define _TASK_CONFIG_DEFINE(task)         __TASK_CONFIG_DEFINE(__DEBRACKET task)

/* clang-format off */

/**
 * @brief Instantiate tasks information for task runner
 *
 * Helper macro that automatically creates the 3 items needed for correct
 * operation of the task runner:
 *    1. Variably sized memory for the task (e.g. thread stack)
 *    2. Array of task configuration structs for the runner
 *    3. Array of task data structs for the runner
 *
 * Example Usage:
 * @code{.c}
 * #define SLEEPY_TASK(define_mem, define_config, dev_pointer)                  \
 *    IF_ENABLED(define_mem,                                                    \
 *               (K_THREAD_STACK_DEFINE(sleep_stack_area, 1024);                \
 *                struct k_thread sleep_thread_obj))                            \
 *    IF_ENABLED(define_config,                                                 \
 *        ({                                                                    \
 *            .name = "sleepy",                                                 \
 *            .task_id = TASK_ID_SLEEPY,                                        \
 *            .task_args.const_arg = dev_pointer,                               \
 *            .exec_type = TASK_EXECUTOR_THREAD,                                \
 *            .executor.thread = {                                              \
 *                .thread = &sleep_thread_obj,                                  \
 *                .task_fn = example_task_fn,                                   \
 *                .thread_stack = sleep_stack_area,                             \
 *                .thread_stack_size = K_THREAD_STACK_SIZEOF(sleep_stack_area), \
 *            },
 *        }))
 *
 * #define WORKQ_TASK(define_mem, define_config, ptr)  \
 *     IF_ENABLED(define_config,                       \
 *         ({                                          \
 *             .name = "workq",                        \
 *             .task_id = TASK_ID_WORKQ,               \
 *             .task_args.arg = ptr,                   \
 *             .exec_type = TASK_EXECUTOR_WORKQUEUE,   \
 *             .executor.workqueue = {                 \
 *                 .worker_fn = example_workqueue_fn,  \
 *             },                                      \
 *         }))
 *
 * TASK_RUNNER_TASKS_DEFINE(config, data,
 *   (SLEEPY_TASK, DEVICE_DT_GET(DT_NODELABEL(dev))),
 *   (WORKQ_TASK, &some_pointer));
 * @endcode
 *
 * @param config_name Name of the created @ref task_config array
 * @param data_name Name of the created @ref task_data array
 * @param ... Grouped list of task definition macros and their arguments
 */
#define TASK_RUNNER_TASKS_DEFINE(config_name, data_name, ...)                                      \
	/* Define task specific variables */                                                       \
	FOR_EACH_NONEMPTY_TERM(_TASK_VAR_DEFINE, (;), __VA_ARGS__);                                \
	/* Define the configurations for each task */                                              \
	static const struct task_config config_name[] = {                                          \
		FOR_EACH_NONEMPTY_TERM(_TASK_CONFIG_DEFINE, (,), __VA_ARGS__)                      \
	};                                                                                         \
	/* Define the runtime task data array */                                                   \
	static struct task_data data_name[ARRAY_SIZE(config_name)]

/* clang-format on */

/**
 * @brief Get the parent task_data struct from the work pointer
 *
 * @param work Work pointer provided to handler
 *
 * @return struct task_data* Parent struct
 */
static inline struct task_data *task_data_from_work(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);

	return CONTAINER_OF(dwork, struct task_data, executor.workqueue.work);
}

/**
 * @brief Retrieve the schedule associated with a task
 *
 * @param data Task data struct
 *
 * @return struct task_schedule* Task schedule
 */
const struct task_schedule *task_schedule_from_data(struct task_data *data);

/**
 * @brief Retrieve per-schedule persistent memory
 *
 * @param data Task data struct
 *
 * @return uint8_t* Pointer to persistent memory of size CONFIG_TASK_RUNNER_PER_SCHEDULE_STORAGE
 */
uint8_t *task_schedule_persistent_storage(struct task_data *data);

/**
 * @brief Reschedule the task to run again after a delay
 *
 * @param task Task data structure
 *
 * @param delay Delay until running again
 */
void task_workqueue_reschedule(struct task_data *task, k_timeout_t delay);

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
	int signaled;
	int result;

	/* Block until requested timeout */
	(void)k_poll(events, 1, timeout);
	/* Determine if we have been requested to terminate */
	k_poll_signal_check(terminate_signal, &signaled, &result);
	return signaled ? 1 : 0;
}

/**
 * @brief Determine if a given TDF was requested by the schedule
 *
 * @param schedule Task schedule to evaluate
 * @param tdf_mask Single TDF mask from activity definition
 *
 * @retval true TDF was requested
 * @retval false TDF was not requested
 */
static inline bool task_schedule_tdf_requested(const struct task_schedule *schedule,
					       uint8_t tdf_mask)
{
	return (schedule->task_logging[0].tdf_mask & tdf_mask) ||
	       (schedule->task_logging[1].tdf_mask & tdf_mask);
}

/**
 * @brief Log an array of TDFs as requested by a schedule as a diff array
 *
 * @param schedule Task schedule
 * @param tdf_mask Single TDF mask that corresponds to @a tdf_id
 * @param tdf_id TDF sensor ID
 * @param tdf_len Length of a single TDF
 * @param tdf_num Number of TDFs to log
 * @param format TDF encoding mode
 * @param time Epoch time associated with the first TDF. 0 for no timestamp.
 * @param idx_period Index of first sample if @a format == TDF_DATA_FORMAT_IDX_ARRAY
 *                   Epoch time between tdfs when @a tdf_num > 0 otherwise.
 * @param data TDF data array
 */
static inline void task_schedule_tdf_log_core(const struct task_schedule *schedule,
					      uint8_t tdf_mask, uint16_t tdf_id, uint8_t tdf_len,
					      uint8_t tdf_num, enum tdf_data_format format,
					      uint64_t time, uint32_t idx_period, const void *data)
{
	if (schedule->task_logging[0].tdf_mask & tdf_mask) {
		tdf_data_logger_log_core(schedule->task_logging[0].loggers, tdf_id, tdf_len,
					 tdf_num, format, time, idx_period, data);
	}
	if (schedule->task_logging[1].tdf_mask & tdf_mask) {
		tdf_data_logger_log_core(schedule->task_logging[1].loggers, tdf_id, tdf_len,
					 tdf_num, format, time, idx_period, data);
	}
}

/**
 * @brief Log an array of TDFs as requested by a schedule
 *
 * @param schedule Task schedule
 * @param tdf_mask Single TDF mask that corresponds to @a tdf_id
 * @param tdf_id TDF sensor ID
 * @param tdf_len Length of a single TDF
 * @param tdf_num Number of TDFs to log
 * @param time Epoch time associated with the first TDF. 0 for no timestamp.
 * @param period Time period between the TDF samples
 * @param data TDF data array
 */
static inline void task_schedule_tdf_log_array(const struct task_schedule *schedule,
					       uint8_t tdf_mask, uint16_t tdf_id, uint8_t tdf_len,
					       uint8_t tdf_num, uint64_t time, uint32_t period,
					       const void *data)
{
	task_schedule_tdf_log_core(schedule, tdf_mask, tdf_id, tdf_len, tdf_num,
				   TDF_DATA_FORMAT_TIME_ARRAY, time, period, data);
}

/**
 * @brief Log a single TDF as requested by a schedule
 *
 * @param schedule Task schedule
 * @param tdf_mask Single TDF mask that corresponds to @a tdf_id
 * @param tdf_id TDF sensor ID
 * @param tdf_len Length of a single TDF
 * @param time Epoch time associated with the first TDF. 0 for no timestamp.
 * @param data TDF data array
 */
static inline void task_schedule_tdf_log(const struct task_schedule *schedule, uint8_t tdf_mask,
					 uint16_t tdf_id, uint8_t tdf_len, uint64_t time,
					 const void *data)
{
	task_schedule_tdf_log_core(schedule, tdf_mask, tdf_id, tdf_len, 1, TDF_DATA_FORMAT_SINGLE,
				   time, 0, data);
}

/**
 * @brief Type safe wrapper around @ref task_schedule_tdf_log_array
 *
 * Adds compile-time validation that the passed pointer matches the type associated
 * with @a tdf_id.
 *
 * @note Only works for TDF types without trailing variable length arrays
 *
 * @param schedule Task schedule
 * @param tdf_mask Single TDF mask that corresponds to @a tdf_id
 * @param tdf_id TDF sensor ID
 * @param tdf_num Number of TDFs to log
 * @param tdf_time Epoch time associated with the first TDF. 0 for no timestamp.
 * @param period Time period between the TDF samples
 * @param data TDF data array
 */
#define TASK_SCHEDULE_TDF_LOG_ARRAY(schedule, tdf_mask, tdf_id, tdf_num, tdf_time, period, data)   \
	task_schedule_tdf_log_array(schedule, tdf_mask, tdf_id, sizeof(TDF_TYPE(tdf_id)), tdf_num, \
				    tdf_time, period, data);                                       \
	do {                                                                                       \
		__maybe_unused const TDF_TYPE(tdf_id) *_data = data;                               \
	} while (0)

/**
 * @brief Type safe wrapper around @ref task_schedule_tdf_log
 *
 * Adds compile-time validation that the passed pointer matches the type associated
 * with @a tdf_id.
 *
 * @note Only works for TDF types without trailing variable length arrays
 *
 * @param schedule Task schedule
 * @param tdf_mask Single TDF mask that corresponds to @a tdf_id
 * @param tdf_id TDF sensor ID
 * @param tdf_time Epoch time associated with the first TDF. 0 for no timestamp.
 * @param data TDF data array
 */
#define TASK_SCHEDULE_TDF_LOG(schedule, tdf_mask, tdf_id, tdf_time, data)                          \
	task_schedule_tdf_log(schedule, tdf_mask, tdf_id, sizeof(TDF_TYPE(tdf_id)), tdf_time,      \
			      data);                                                               \
	do {                                                                                       \
		__maybe_unused const TDF_TYPE(tdf_id) *_data = data;                               \
	} while (0)

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASK_H_ */
