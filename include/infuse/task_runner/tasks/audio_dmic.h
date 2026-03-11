/**
 * @file
 * @brief Digital microphone task
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_AUDIO_DMIC_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_AUDIO_DMIC_H_

#include <zephyr/kernel.h>
#include <zephyr/audio/dmic.h>

#include <infuse/drivers/imu.h>
#include <infuse/task_runner/task.h>
#include <infuse/task_runner/schedule.h>

#include <infuse/task_runner/tasks/infuse_task_ids.h>

#ifdef __cplusplus
extern "C" {
#endif

struct task_audio_dmic_config {
	const struct device *dev;
	struct pdm_io_cfg io_cfg;
	enum pdm_lr pdm_chan;
};

/**
 * @brief Digital microphone task function
 *
 * @param schedule Schedule that triggered task
 * @param terminate Terminate request from task runner
 * @param dmic_dev Digital microphone to use
 */
void dmic_task_fn(const struct task_schedule *schedule, struct k_poll_signal *terminate,
		  void *dmic_dev);

/* Helper macro for defining static memory */
#define _AUDIO_DMIC_MEM(dmic_node)                                                                 \
	K_THREAD_STACK_DEFINE(dmic_stack_area, CONFIG_TASK_RUNNER_TASK_AUDIO_DMIC_STACK_SIZE);     \
	static const struct task_audio_dmic_config dmic_config = {                                 \
		.dev = DEVICE_DT_GET(dmic_node),                                                   \
		.io_cfg = PDM_DT_IO_CFG_GET(dmic_node),                                            \
		.pdm_chan = PDM_DT_HAS_LEFT_CHANNEL(dmic_node) ? PDM_CHAN_LEFT : PDM_CHAN_RIGHT,   \
	};                                                                                         \
	struct k_thread dmic_thread_obj

/* Helper macro for defining congiguration */
#define _AUDIO_DMIC_CONFIG(dmic_node)                                                              \
	{                                                                                          \
		.name = "mic",                                                                     \
		.task_id = TASK_ID_AUDIO_DMIC,                                                     \
		.exec_type = TASK_EXECUTOR_THREAD,                                                 \
		.task_arg.const_arg = &dmic_config,                                                \
		.executor.thread =                                                                 \
			{                                                                          \
				.thread = &dmic_thread_obj,                                        \
				.task_fn = dmic_task_fn,                                           \
				.stack = dmic_stack_area,                                          \
				.stack_size = K_THREAD_STACK_SIZEOF(dmic_stack_area),              \
			},                                                                         \
	}

/**
 * @brief Digital microphone task
 *
 * @param define_mem Define memory
 * @param define_config Define task
 * @param dmic_node Digital microphone node label
 */
#define AUDIO_DMIC_TASK(define_mem, define_config, dmic_node)                                      \
	IF_ENABLED(define_mem, (_AUDIO_DMIC_MEM(dmic_node)))                                       \
	IF_ENABLED(define_config, (_AUDIO_DMIC_CONFIG(dmic_node)))

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_AUDIO_DMIC_H_ */
