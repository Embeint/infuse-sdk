/**
 * @file
 * @brief Specialised driver API for IMU devices
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 *
 * @details
 * Driver API optimised for high datarate, FIFO buffered IMU sensors.
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_IMU_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_IMU_H_

#include <zephyr/kernel.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Infuse IMU API
 * @defgroup infuse_imu_apis IMU API
 * @{
 */

/* IMU configuration struct */
struct imu_config {
	struct {
		/* Sample rate in Hertz */
		uint16_t sample_rate_hz;
		/* Full scale range in G */
		uint8_t full_scale_range;
		/* True for low power mode, false for performance */
		bool low_power;
	} accelerometer;
	struct {
		/* Sample rate in Hertz */
		uint16_t sample_rate_hz;
		/* Full scale range in deg/s */
		uint16_t full_scale_range;
		/* True for low power mode, false for performance */
		bool low_power;
	} gyroscope;
	struct {
		uint16_t sample_rate_hz;
	} magnetometer;
	/* Number of sample to buffer in FIFO before raising interrupt */
	uint16_t fifo_sample_buffer;
};

/* Configured IMU value */
struct imu_config_output {
	/* Expected period between accelerometer samples */
	uint32_t accelerometer_period_us;
	/* Expected period between gyroscope samples */
	uint32_t gyroscope_period_us;
	/* Expected period between magnetometer samples */
	uint32_t magnetometer_period_us;
	/* Expected period FIFO interrupts */
	uint32_t expected_interrupt_period_us;
};

struct imu_sample {
	int16_t x;
	int16_t y;
	int16_t z;
} __packed;

/* Metadata for each sub-sensor in a FIFO buffer */
struct imu_sensor_meta {
	/* Local tick counter of first sample */
	int64_t timestamp_ticks;
	/* Real period between first and last samples in buffer */
	uint32_t buffer_period_ticks;
	/* Accel = G, Gyro = DPS, Mag = ? */
	uint16_t full_scale_range;
	/* Offset into sample array of sensor */
	uint8_t offset;
	/* Number of readings for this sensor */
	uint8_t num;
};

/* FIFO read structure */
struct imu_sample_array {
	/* Metadata for accelerometer samples */
	struct imu_sensor_meta accelerometer;
	/* Metadata for gyroscope samples */
	struct imu_sensor_meta gyroscope;
	/* Metadata for magnetometer samples */
	struct imu_sensor_meta magnetometer;
	/* Linear array of all samples */
	struct imu_sample samples[];
};

/* Create type that holds a given number of IMU samples */
#define IMU_SAMPLE_ARRAY_TYPE_DEFINE(type_name, max_samples)                                       \
	struct type_name {                                                                         \
		struct imu_sample_array header;                                                    \
		struct imu_sample samples[max_samples];                                            \
	}

/* Create static buffer of IMU samples suitable for use with @ref imu_data_read */
#define IMU_SAMPLE_ARRAY_CREATE(name, max_samples)                                                 \
	IMU_SAMPLE_ARRAY_TYPE_DEFINE(_anon_t_##name, max_samples);                                 \
	static struct _anon_t_##name _anon_##name;                                                 \
	static struct imu_sample_array *name = (void *)&_anon_##name

struct infuse_imu_api {
	int (*configure)(const struct device *dev, const struct imu_config *config,
			 struct imu_config_output *output);
	int (*data_wait)(const struct device *dev, k_timeout_t timeout);
	int (*data_read)(const struct device *dev, struct imu_sample_array *samples,
			 uint16_t max_samples);
};

/**
 * @brief Configure IMU for operation
 *
 * @param dev IMU to configure
 * @param config Desired sensor configuration, or NULL to disable
 * @param output Configured sensor timings
 *
 * @retval 0 on success
 * @retval -EINVAL on invalid parameters
 * @retval -errno on error
 */
static inline int imu_configure(const struct device *dev, const struct imu_config *config,
				struct imu_config_output *output)
{
	const struct infuse_imu_api *api = dev->api;

	return api->configure(dev, config, output);
}

/**
 * @brief Wait for FIFO interrupt from IMU
 *
 * @param dev IMU to wait on
 * @param timeout Duration to wait for
 *
 * @retval 0 on success
 * @retval -EAGAIN on timeout
 */
static inline int imu_data_wait(const struct device *dev, k_timeout_t timeout)
{
	const struct infuse_imu_api *api = dev->api;

	return api->data_wait(dev, timeout);
}

/**
 * @brief Read samples from IMU
 *
 * @param dev IMU to read from
 * @param samples Sample buffer to read into
 * @param max_samples Maximum number of samples to populate
 *
 * @retval 0 on success
 * @retval -ENOMEM more than @a max_samples samples buffered
 * @retval -errno on error
 */
static inline int imu_data_read(const struct device *dev, struct imu_sample_array *samples,
				uint16_t max_samples)
{
	const struct infuse_imu_api *api = dev->api;

	return api->data_read(dev, samples, max_samples);
}

/**
 * @brief Convert a full scale range to an expected value for 1G
 *
 * @param full_scale Full scale range in G's (e.g 4 for +- 4G)
 *
 * @return int16_t value expected for 1G
 */
static inline int16_t imu_accelerometer_1g(uint8_t full_scale)
{
	switch (full_scale) {
	case 2:
		return 16384;
	case 4:
		return 8192;
	case 8:
		return 4096;
	case 16:
		return 2048;
	}
	return -1;
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_IMU_H_ */
