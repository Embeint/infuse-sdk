/**
 * @file
 * @brief Digital microphone task arguments
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_AUDIO_DMIC_ARGS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_AUDIO_DMIC_ARGS_H_

#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	/* Log audio metadata (sample rate) */
	TASK_AUDIO_DMIC_LOG_METADATA = BIT(0),
	/* Log raw audio samples */
	TASK_AUDIO_DMIC_LOG_SAMPLES = BIT(1),
};

/** @brief AUDIO_DMIC task arguments */
struct task_audio_dmic_args {
	/* Requested sample rate in Hz */
	uint32_t sample_rate_hz;
	/* Sample gain */
	int8_t gain_db;
} __packed;

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_AUDIO_DMIC_ARGS_H_ */
