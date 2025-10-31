/**
 * @file
 * @brief IMU task arguments
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_IMU_ARGS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_IMU_ARGS_H_

#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	TASK_IMU_LOG_ACC = BIT(0),
	TASK_IMU_LOG_GYR = BIT(1),
	TASK_IMU_LOG_MAG = BIT(2),
};

enum {
	TASK_IMU_FLAGS_LOW_POWER_MODE = BIT(0),
};

/** @brief IMU task arguments */
struct task_imu_args {
	/** Accelerometer configuration  */
	struct {
		uint16_t rate_hz;
		uint8_t range_g;
		uint8_t pad;
	} __packed accelerometer;
	/** Gyroscope configuration  */
	struct {
		uint16_t rate_hz;
		uint16_t range_dps;
		uint8_t pad;
	} __packed gyroscope;
	/** Magnetometer configuration  */
	struct {
		uint16_t rate_hz;
		uint8_t range_gauss;
		uint8_t pad;
	} __packed magnetometer;
	/** Requested number of samples to buffer in FIFO */
	uint16_t fifo_sample_buffer;
	/** Run for this many buffers then terminate */
	uint8_t num_buffers;
	/** Operational flags */
	uint8_t flags;
} __packed;

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_IMU_ARGS_H_ */
