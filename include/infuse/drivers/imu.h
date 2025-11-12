/**
 * @file
 * @brief Specialised driver API for IMU devices
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 * Driver API optimised for high datarate, FIFO buffered IMU sensors.
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_IMU_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_IMU_H_

#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include <infuse/drivers/imu/data_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Infuse IMU API
 * @defgroup infuse_imu_apis IMU API
 * @{
 */

/** IMU configuration struct */
struct imu_config {
	struct {
		/** Sample rate in Hertz */
		uint16_t sample_rate_hz;
		/** Full scale range in G */
		uint8_t full_scale_range;
		/** True for low power mode, false for performance */
		bool low_power;
	} accelerometer;
	struct {
		/** Sample rate in Hertz */
		uint16_t sample_rate_hz;
		/** Full scale range in deg/s */
		uint16_t full_scale_range;
		/** True for low power mode, false for performance */
		bool low_power;
	} gyroscope;
	struct {
		uint16_t sample_rate_hz;
	} magnetometer;
	/** Requested number of samples to buffer in FIFO before raising interrupt */
	uint16_t fifo_sample_buffer;
};

/** Configured IMU value */
struct imu_config_output {
	/** Expected period between accelerometer samples */
	uint32_t accelerometer_period_us;
	/** Expected period between gyroscope samples */
	uint32_t gyroscope_period_us;
	/** Expected period between magnetometer samples */
	uint32_t magnetometer_period_us;
	/** Expected period FIFO interrupts */
	uint32_t expected_interrupt_period_us;
};

struct infuse_imu_api {
	int (*configure)(const struct device *dev, const struct imu_config *config,
			 struct imu_config_output *output);
	int (*data_wait)(const struct device *dev, k_timeout_t timeout);
	int (*data_read)(const struct device *dev, struct imu_sample_array *samples,
			 uint16_t max_samples);
#if defined(CONFIG_INFUSE_IMU_SELF_TEST) || defined(__DOXYGEN__)
	int (*self_test)(const struct device *dev);
#endif /* defined(CONFIG_INFUSE_IMU_SELF_TEST) || defined(__DOXYGEN__) */
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
 * @retval 1 on success, but FIFO frames have been lost
 * @retval -ENOMEM more than @a max_samples samples buffered
 * @retval -errno on error
 */
static inline int imu_data_read(const struct device *dev, struct imu_sample_array *samples,
				uint16_t max_samples)
{
	const struct infuse_imu_api *api = dev->api;

	return api->data_read(dev, samples, max_samples);
}

#if defined(CONFIG_INFUSE_IMU_SELF_TEST) || defined(__DOXYGEN__)
/**
 * @brief Run self-test functionality on the IMU
 *
 * @param dev IMU to run self-test on
 *
 * @retval 0 on success
 * @retval -errno on error
 */
static inline int imu_self_test(const struct device *dev)
{
	const struct infuse_imu_api *api = dev->api;

	if (api->self_test == NULL) {
		return -ENOTSUP;
	}
	return api->self_test(dev);
}
#endif /* defined(CONFIG_INFUSE_IMU_SELF_TEST) || defined(__DOXYGEN__) */

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
	default:
		return -1;
	}
}

/**
 * @brief Get the local ticks between samples in a buffer
 *
 * @param meta Metadata for the relevant channel in a buffer
 *
 * @return uint32_t Ticks between samples in the buffer
 */
static inline uint32_t imu_sample_period(const struct imu_sensor_meta *meta)
{
	if (meta->num < 2) {
		return 0;
	}
	return meta->buffer_period_ticks / (meta->num - 1);
}

/**
 * @brief Get the approximate sample rate of a buffer
 *
 * @param meta Metadata for the relevant channel in a buffer
 *
 * @return uint16_t Approximate sample rate in Hertz
 */
static inline uint16_t imu_sample_rate(const struct imu_sensor_meta *meta)
{
	uint32_t period_us = k_ticks_to_us_near32(imu_sample_period(meta));

	if (period_us == 0) {
		return 0;
	}
	return 1000000 / period_us;
}

/**
 * @brief Get the local tick counter of a given sample in a buffer
 *
 * @param meta Metadata for the relevant channel in a buffer
 * @param sample Sample index to get timestamp for
 *
 * @return k_ticks_t Timestamp for sample N in the buffer
 */
static inline k_ticks_t imu_sample_timestamp(const struct imu_sensor_meta *meta, uint8_t sample)
{
	if (meta->num < 2) {
		return meta->timestamp_ticks;
	}
	return meta->timestamp_ticks + (sample * meta->buffer_period_ticks / (meta->num - 1));
}

/** State for @ref imu_linear_downsample_scaled */
struct imu_linear_downsample_scaled_state {
	/** Private */
	struct imu_sample last_sample;
	/** Buffer storage for X axis output */
	float *output_x;
	/** Buffer storage for Y axis output */
	float *output_y;
	/** Buffer storage for Z axis output */
	float *output_z;
	/** Size of the axis output arrays */
	uint16_t output_size;
	/** Current number of samples written to output */
	uint16_t output_offset;
	/** Output is scaled as (integer_val/scale) */
	int16_t scale;
	/** Multiplier applied to input frequency */
	uint8_t freq_mult;
	/** Divider applied to (input_frequency * freq_mult) */
	uint8_t freq_div;
	/** Private */
	uint8_t subsample_idx;
};

/**
 * @brief Downsample IMU samples to a new frequency using linear interpolation
 *
 * Function returns as soon as state.output_offset == state.output_size.
 * Function should be called again with remaining samples once output buffer has
 * been processed and state.output_offset reset to 0.
 *
 * output_frequency = input_frequency * freq_mult / freq_div
 *
 * @param state State structure
 * @param input Input IMU sample buffer
 * @param num_input Number of samples in @a input
 *
 * @retval number of input samples consumed
 */
int imu_linear_downsample_scaled(struct imu_linear_downsample_scaled_state *state,
				 const struct imu_sample *input, uint16_t num_input);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_IMU_H_ */
