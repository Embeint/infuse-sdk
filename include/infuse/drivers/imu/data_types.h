/**
 * @file
 * @brief IMU Output data types
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_IMU_DATA_TYPES_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_IMU_DATA_TYPES_H_

#include <stdint.h>

#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief IMU data types
 * @defgroup imu_data_types_apis IMU Data Types API
 * @{
 */

/** IMU sample struct */
struct imu_sample {
	int16_t x;
	int16_t y;
	int16_t z;
};
/* Validate __packed attribute is not required */
BUILD_ASSERT(sizeof(struct imu_sample) == 6);
BUILD_ASSERT(_Alignof(struct imu_sample) == 2);

/** Metadata for each sub-sensor in a FIFO buffer */
struct imu_sensor_meta {
	/** Local tick counter of first sample */
	int64_t timestamp_ticks;
	/** Real period between first and last samples in buffer */
	uint32_t buffer_period_ticks;
	/** Accel = G, Gyro = DPS, Mag = ? */
	uint16_t full_scale_range;
	/** Offset into sample array of sensor */
	uint16_t offset;
	/** Number of readings for this sensor */
	uint16_t num;
};

/** FIFO read structure */
struct imu_sample_array {
	/** Metadata for accelerometer samples */
	struct imu_sensor_meta accelerometer;
	/** Metadata for gyroscope samples */
	struct imu_sensor_meta gyroscope;
	/** Metadata for magnetometer samples */
	struct imu_sensor_meta magnetometer;
	/** Linear array of all samples */
	struct imu_sample samples[];
};

/** Create type that holds a given number of IMU samples */
#define IMU_SAMPLE_ARRAY_TYPE_DEFINE(type_name, max_samples)                                       \
	struct type_name {                                                                         \
		struct imu_sample_array header;                                                    \
		struct imu_sample samples[max_samples];                                            \
	}

/** Create static buffer of IMU samples suitable for use with @ref imu_data_read */
#define IMU_SAMPLE_ARRAY_CREATE(name, max_samples)                                                 \
	IMU_SAMPLE_ARRAY_TYPE_DEFINE(_anon_t_##name, max_samples);                                 \
	static struct _anon_t_##name _anon_##name;                                                 \
	static struct imu_sample_array *name = (void *)&_anon_##name

/* Accelerometer magnitude broadcast structure */
struct imu_magnitude_array {
	/* Metadata for magnitude samples */
	struct imu_sensor_meta meta;
	/* Linear array of all magnitudes */
	uint32_t magnitudes[];
};

/* Create type that holds a given number of IMU magnitude samples */
#define IMU_MAG_ARRAY_TYPE_DEFINE(type_name, max_samples)                                          \
	struct type_name {                                                                         \
		struct imu_sensor_meta meta;                                                       \
		uint32_t magnitudes[max_samples];                                                  \
	}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_IMU_DATA_TYPES_H_ */
