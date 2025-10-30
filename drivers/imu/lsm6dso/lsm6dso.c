/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * Configuration information contained in AN5192
 */

#define DT_DRV_COMPAT st_lsm6dso

#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/drivers/imu.h>

#include "lsm6dso.h"

#define FIFO_BYTES MIN(LSM6DSO_FIFO_SIZE, (7 * CONFIG_INFUSE_IMU_MAX_FIFO_SAMPLES))

struct lsm6dso_config {
	union lsm6dso_bus bus;
	const struct lsm6dso_bus_io *bus_io;
	struct gpio_dt_spec int1_gpio;
};

struct lsm6dso_data {
	struct gpio_callback int1_cb;
	struct k_sem int1_sem;
	int64_t int1_timestamp;
	int64_t int1_prev_timestamp;
	uint16_t acc_time_scale;
	uint16_t gyr_time_scale;
	uint16_t gyro_range;
	uint8_t accel_range;
	uint16_t fifo_threshold;
	uint8_t fifo_data_buffer[FIFO_BYTES];
};

struct fifo_frame {
	uint8_t tag;
	int16_t x;
	int16_t y;
	int16_t z;
} __packed;

struct sensor_config {
	uint32_t period_us;
	uint8_t ctrl_config;
	uint8_t fifo_config;
	bool low_power;
};

LOG_MODULE_REGISTER(lsm6dso, CONFIG_SENSOR_LOG_LEVEL);

static inline int lsm6dso_bus_check(const struct device *dev)
{
	const struct lsm6dso_config *cfg = dev->config;

	return cfg->bus_io->check(&cfg->bus);
}

static inline int lsm6dso_bus_init(const struct device *dev)
{
	const struct lsm6dso_config *cfg = dev->config;

	return cfg->bus_io->init(&cfg->bus);
}

static inline int lsm6dso_reg_read(const struct device *dev, uint8_t reg, void *data,
				   uint16_t length)
{
	const struct lsm6dso_config *cfg = dev->config;

	return cfg->bus_io->read(&cfg->bus, reg, data, length);
}

static inline int lsm6dso_reg_write(const struct device *dev, uint8_t reg, const void *data,
				    uint16_t length)
{
	const struct lsm6dso_config *cfg = dev->config;

	return cfg->bus_io->write(&cfg->bus, reg, data, length);
}

static int lsm6dso_low_power_reset(const struct device *dev)
{
	const struct lsm6dso_config *cfg = dev->config;
	struct lsm6dso_data *data = dev->data;
	uint8_t reg_val;
	int rc;

	(void)gpio_pin_interrupt_configure_dt(&cfg->int1_gpio, GPIO_INT_DISABLE);
	(void)gpio_pin_configure_dt(&cfg->int1_gpio, GPIO_DISCONNECTED);
	(void)k_sem_take(&data->int1_sem, K_NO_WAIT);

	/* Soft-reset the device */
	reg_val = LSM6DSO_CTRL3_C_SW_RESET;
	rc = lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL3_C, &reg_val, 1);
	if (rc < 0) {
		return rc;
	}
	/* Wait for the software reset to complete */
	k_sleep(K_MSEC(15));
	/* Enable BDU (IF_INC set by default) */
	reg_val = LSM6DSO_CTRL3_C_BDU | LSM6DSO_CTRL3_C_IF_INC;
	return lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL3_C, &reg_val, 1);
}

static struct sensor_config accel_conf(uint16_t sample_rate, uint8_t range, bool low_power,
				       uint8_t *fs_range)
{
	struct sensor_config ret;

	/* Sensing range */
	*fs_range = range;
	switch (range) {
	case 2:
		ret.ctrl_config = LSM6DSO_CTRL1_XL_RANGE_2G;
		break;
	case 4:
		ret.ctrl_config = LSM6DSO_CTRL1_XL_RANGE_4G;
		break;
	case 8:
		ret.ctrl_config = LSM6DSO_CTRL1_XL_RANGE_8G;
		break;
	case 16:
		ret.ctrl_config = LSM6DSO_CTRL1_XL_RANGE_16G;
		break;
	default:
		LOG_WRN("Default range 4G");
		*fs_range = 4;
		ret.ctrl_config = LSM6DSO_CTRL1_XL_RANGE_4G;
	}

	/* Sample rate selection */
	if (sample_rate < 7) {
		ret.period_us = 16 * USEC_PER_SEC / 26;
		ret.ctrl_config |= LSM6DSO_CTRL1_XL_ODR_1HZ6;
		ret.fifo_config = LSM6DSO_FIFO_CTRL3_BDR_XL_1HZ5;
		low_power = true;
	} else if (sample_rate < 23) {
		ret.period_us = 2 * USEC_PER_SEC / 26;
		ret.ctrl_config |= LSM6DSO_CTRL1_XL_ODR_12HZ5;
		ret.fifo_config = LSM6DSO_FIFO_CTRL3_BDR_XL_12HZ5;
	} else if (sample_rate < 45) {
		ret.period_us = USEC_PER_SEC / 26;
		ret.ctrl_config |= LSM6DSO_CTRL1_XL_ODR_26HZ;
		ret.fifo_config = LSM6DSO_FIFO_CTRL3_BDR_XL_26HZ;
	} else if (sample_rate < 78) {
		ret.period_us = USEC_PER_SEC / 52;
		ret.ctrl_config |= LSM6DSO_CTRL1_XL_ODR_52HZ;
		ret.fifo_config = LSM6DSO_FIFO_CTRL3_BDR_XL_52HZ;
	} else if (sample_rate < 156) {
		ret.period_us = USEC_PER_SEC / 104;
		ret.ctrl_config |= LSM6DSO_CTRL1_XL_ODR_104HZ;
		ret.fifo_config = LSM6DSO_FIFO_CTRL3_BDR_XL_104HZ;
	} else if (sample_rate < 312) {
		ret.period_us = USEC_PER_SEC / 208;
		ret.ctrl_config |= LSM6DSO_CTRL1_XL_ODR_208HZ;
		ret.fifo_config = LSM6DSO_FIFO_CTRL3_BDR_XL_208HZ;
	} else if (sample_rate < 624) {
		ret.period_us = USEC_PER_SEC / 416;
		ret.ctrl_config |= LSM6DSO_CTRL1_XL_ODR_416HZ;
		ret.fifo_config = LSM6DSO_FIFO_CTRL3_BDR_XL_416HZ;
	} else if (sample_rate < 1248) {
		ret.period_us = USEC_PER_SEC / 833;
		ret.ctrl_config |= LSM6DSO_CTRL1_XL_ODR_833HZ;
		ret.fifo_config = LSM6DSO_FIFO_CTRL3_BDR_XL_833HZ;
	} else if (sample_rate < 2496) {
		ret.period_us = USEC_PER_SEC / 1667;
		ret.ctrl_config |= LSM6DSO_CTRL1_XL_ODR_1667HZ;
		ret.fifo_config = LSM6DSO_FIFO_CTRL3_BDR_XL_1667HZ;
	} else if (sample_rate < 4992) {
		ret.period_us = USEC_PER_SEC / 3333;
		ret.ctrl_config |= LSM6DSO_CTRL1_XL_ODR_3333HZ;
		ret.fifo_config = LSM6DSO_FIFO_CTRL3_BDR_XL_3333HZ;
	} else {
		ret.period_us = USEC_PER_SEC / 6667;
		ret.ctrl_config |= LSM6DSO_CTRL1_XL_ODR_6667HZ;
		ret.fifo_config = LSM6DSO_FIFO_CTRL3_BDR_XL_6667HZ;
	}
	/* High performance mode MUST be used above 208 Hz */
	if (sample_rate >= 312) {
		low_power = false;
	}
	ret.low_power = low_power;
	return ret;
}

static struct sensor_config gyr_conf(uint16_t sample_rate, uint16_t range, bool low_power,
				     uint16_t *fs_range)
{
	struct sensor_config ret;

	/* Sensing range */
	*fs_range = range;
	switch (range) {
	case 125:
		ret.ctrl_config = LSM6DSO_CTRL2_G_FS_125DPS;
		break;
	case 250:
		ret.ctrl_config = LSM6DSO_CTRL2_G_FS_250DPS;
		break;
	case 500:
		ret.ctrl_config = LSM6DSO_CTRL2_G_FS_500DPS;
		break;
	case 1000:
		ret.ctrl_config = LSM6DSO_CTRL2_G_FS_1000DPS;
		break;
	case 2000:
		ret.ctrl_config = LSM6DSO_CTRL2_G_FS_2000DPS;
		break;
	default:
		LOG_WRN("Default range 4G");
		*fs_range = 1000;
		ret.ctrl_config = LSM6DSO_CTRL2_G_FS_1000DPS;
	}

	/* Sample rate selection */
	if (sample_rate < 23) {
		ret.period_us = 2 * USEC_PER_SEC / 26;
		ret.ctrl_config |= LSM6DSO_CTRL2_G_ODR_12HZ5;
		ret.fifo_config = LSM6DSO_FIFO_CTRL3_BDR_GY_12HZ5;
	} else if (sample_rate < 45) {
		ret.period_us = USEC_PER_SEC / 26;
		ret.ctrl_config |= LSM6DSO_CTRL2_G_ODR_26HZ;
		ret.fifo_config = LSM6DSO_FIFO_CTRL3_BDR_GY_26HZ;
	} else if (sample_rate < 78) {
		ret.period_us = USEC_PER_SEC / 52;
		ret.ctrl_config |= LSM6DSO_CTRL2_G_ODR_52HZ;
		ret.fifo_config = LSM6DSO_FIFO_CTRL3_BDR_GY_52HZ;
	} else if (sample_rate < 156) {
		ret.period_us = USEC_PER_SEC / 104;
		ret.ctrl_config |= LSM6DSO_CTRL2_G_ODR_104HZ;
		ret.fifo_config = LSM6DSO_FIFO_CTRL3_BDR_GY_104HZ;
	} else if (sample_rate < 312) {
		ret.period_us = USEC_PER_SEC / 208;
		ret.ctrl_config |= LSM6DSO_CTRL2_G_ODR_208HZ;
		ret.fifo_config = LSM6DSO_FIFO_CTRL3_BDR_GY_208HZ;
	} else if (sample_rate < 624) {
		ret.period_us = USEC_PER_SEC / 416;
		ret.ctrl_config |= LSM6DSO_CTRL2_G_ODR_416HZ;
		ret.fifo_config = LSM6DSO_FIFO_CTRL3_BDR_GY_416HZ;
	} else if (sample_rate < 1248) {
		ret.period_us = USEC_PER_SEC / 833;
		ret.ctrl_config |= LSM6DSO_CTRL2_G_ODR_833HZ;
		ret.fifo_config = LSM6DSO_FIFO_CTRL3_BDR_GY_833HZ;
	} else if (sample_rate < 2496) {
		ret.period_us = USEC_PER_SEC / 1667;
		ret.ctrl_config |= LSM6DSO_CTRL2_G_ODR_1667HZ;
		ret.fifo_config = LSM6DSO_FIFO_CTRL3_BDR_GY_1667HZ;
	} else if (sample_rate < 4992) {
		ret.period_us = USEC_PER_SEC / 3333;
		ret.ctrl_config |= LSM6DSO_CTRL2_G_ODR_3333HZ;
		ret.fifo_config = LSM6DSO_FIFO_CTRL3_BDR_GY_3333HZ;
	} else {
		ret.period_us = USEC_PER_SEC / 6667;
		ret.ctrl_config |= LSM6DSO_CTRL2_G_ODR_6667HZ;
		ret.fifo_config = LSM6DSO_FIFO_CTRL3_BDR_GY_6667HZ;
	}
	/* High performance mode MUST be used above 208 Hz */
	if (sample_rate >= 312) {
		low_power = false;
	}
	ret.low_power = low_power;
	return ret;
}

static void lsm6dso_gpio_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	struct lsm6dso_data *data = CONTAINER_OF(cb, struct lsm6dso_data, int1_cb);

	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	data->int1_prev_timestamp = data->int1_timestamp;
	data->int1_timestamp = k_uptime_ticks();
	LOG_DBG("");
	k_sem_give(&data->int1_sem);
}

int lsm6dso_configure(const struct device *dev, const struct imu_config *imu_cfg,
		      struct imu_config_output *output)
{
	const struct lsm6dso_config *config = dev->config;
	struct lsm6dso_data *data = dev->data;
	struct sensor_config config_acc = {0};
	struct sensor_config config_gyr = {0};
	uint32_t frame_period_us;
	uint8_t reg_val;
	int rc;

	/* Soft reset back to low power state */
	rc = lsm6dso_low_power_reset(dev);
	if (rc < 0) {
		return rc;
	}

	/* No more work to do */
	if (imu_cfg == NULL || ((imu_cfg->accelerometer.sample_rate_hz == 0) &&
				(imu_cfg->gyroscope.sample_rate_hz == 0))) {
		return 0;
	}
	if (imu_cfg->fifo_sample_buffer == 0) {
		return -EINVAL;
	}

	output->accelerometer_period_us = 0;
	output->gyroscope_period_us = 0;
	output->magnetometer_period_us = 0;
	output->expected_interrupt_period_us = 0;
	frame_period_us = UINT32_MAX;

	/* Configure accelerometer */
	if (imu_cfg->accelerometer.sample_rate_hz) {
		config_acc = accel_conf(imu_cfg->accelerometer.sample_rate_hz,
					imu_cfg->accelerometer.full_scale_range,
					imu_cfg->accelerometer.low_power, &data->accel_range);

		if (config_acc.low_power) {
			/* Low-power and normal mode are automatically determined by the sample rate
			 */
			reg_val = LSM6DSO_CTRL6_C_XL_HIGH_PERFORMANCE_DISABLE;
			rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL1_XL, &reg_val, 1);
		}

		LOG_DBG("Acc period: %d us", config_acc.period_us);
		rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL1_XL, &config_acc.ctrl_config, 1);

		output->accelerometer_period_us = config_acc.period_us;
		frame_period_us = MIN(frame_period_us, output->accelerometer_period_us);
	}

	/* Configure gyroscope */
	if (imu_cfg->gyroscope.sample_rate_hz) {
		config_gyr = gyr_conf(imu_cfg->gyroscope.sample_rate_hz,
				      imu_cfg->gyroscope.full_scale_range,
				      imu_cfg->gyroscope.low_power, &data->gyro_range);

		LOG_DBG("Gyr period: %d us", config_gyr.period_us);
		rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL2_G, &config_gyr.ctrl_config, 1);

		output->gyroscope_period_us = config_gyr.period_us;
		frame_period_us = MIN(frame_period_us, output->gyroscope_period_us);
	}

	/* Relative ratio of accelerometer and gyroscope samples */
	data->acc_time_scale = output->accelerometer_period_us / frame_period_us;
	data->gyr_time_scale = output->gyroscope_period_us / frame_period_us;

	data->fifo_threshold = MIN(imu_cfg->fifo_sample_buffer, (FIFO_BYTES / 7));

	/* Calculate the expected interrupt period for N samples */
	if (output->accelerometer_period_us && output->gyroscope_period_us) {
		uint32_t evts_per_sec = (USEC_PER_SEC / output->accelerometer_period_us) +
					(USEC_PER_SEC / output->gyroscope_period_us);

		output->expected_interrupt_period_us =
			(data->fifo_threshold * USEC_PER_SEC) / evts_per_sec;
	} else if (output->accelerometer_period_us) {
		output->expected_interrupt_period_us =
			output->accelerometer_period_us * data->fifo_threshold;
	} else {
		output->expected_interrupt_period_us =
			output->gyroscope_period_us * data->fifo_threshold;
	}

	/* Configure FIFO threshold, mode and data batching rates  */
	reg_val = data->fifo_threshold & 0xFF;
	rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_FIFO_CTRL1, &reg_val, 1);
	reg_val = (data->fifo_threshold >> 8) & 0xFF;
	rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_FIFO_CTRL2, &reg_val, 1);
	reg_val = config_acc.fifo_config | config_gyr.fifo_config;
	rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_FIFO_CTRL3, &reg_val, 1);
	reg_val = LSM6DSO_FIFO_CTRL4_FIFO_MODE_FIFO;
	rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_FIFO_CTRL4, &reg_val, 1);

	/* Configure INT1 for FIFO threshold */
	reg_val = LSM6DSO_INT1_CTRL_FIFO_TH;
	rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_INT1_CTRL, &reg_val, 1);
	reg_val = LSM6DSO_INT2_CTRL_FIFO_TH;
	rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_INT2_CTRL, &reg_val, 1);

	/* Approximate start time of data collection */
	data->int1_timestamp = k_uptime_ticks();

	/* Enable the interrupt GPIO */
	(void)gpio_pin_configure_dt(&config->int1_gpio, GPIO_INPUT);
	(void)gpio_pin_interrupt_configure_dt(&config->int1_gpio, GPIO_INT_EDGE_TO_ACTIVE);

	return rc ? -EIO : 0;
}

int lsm6dso_data_wait(const struct device *dev, k_timeout_t timeout)
{
	struct lsm6dso_data *data = dev->data;

	return k_sem_take(&data->int1_sem, timeout);
}

int lsm6dso_data_read(const struct device *dev, struct imu_sample_array *samples,
		      uint16_t max_samples)
{
	const struct lsm6dso_config *config = dev->config;
	struct lsm6dso_data *data = dev->data;
	struct fifo_frame *sample;
	int64_t first_frame_time, last_frame_time;
	int32_t int_period_ticks;
	int32_t frame_period_ticks;
	uint16_t data_frames, extra_frames, interrupt_frame;
	uint16_t fifo_status;
	uint16_t fifo_words;
	uint8_t gyr_out = 0, acc_out = 0;
	uint8_t tag, cnt, prv_cnt;
	int rc;

	/* Init sample output */
	samples->accelerometer = (struct imu_sensor_meta){0};
	samples->gyroscope = (struct imu_sensor_meta){0};
	samples->magnetometer = (struct imu_sensor_meta){0};

	samples->accelerometer.full_scale_range = data->accel_range;
	samples->gyroscope.full_scale_range = data->gyro_range;

	/* Get FIFO data length */
	rc = lsm6dso_reg_read(dev, LSM6DSO_REG_FIFO_STATUS1, &fifo_status, sizeof(fifo_status));
	if (rc < 0) {
		return rc;
	}

	/* Limit the number of words that can be read out to our RAM buffer size */
	fifo_words = fifo_status & 0x3FF;
	if ((7 * fifo_words) > sizeof(data->fifo_data_buffer)) {
		fifo_words = sizeof(data->fifo_data_buffer) / 7;
	}
	LOG_DBG("Reading %04X %d bytes", fifo_status, 7 * fifo_words);

	/* A "false" interrupt can be generated while reading the FIFO if another sample is added to
	 * the FIFO as we empty the FIFO past the configured threshold.
	 * Disable the interrupt around the read operation to prevent this from happening.
	 */
	(void)gpio_pin_interrupt_configure_dt(&config->int1_gpio, GPIO_INT_DISABLE);
	rc = lsm6dso_reg_read(dev, LSM6DSO_REG_FIFO_DATA_OUT_TAG, data->fifo_data_buffer,
			      7 * fifo_words);
	(void)gpio_pin_interrupt_configure_dt(&config->int1_gpio, GPIO_INT_EDGE_TO_ACTIVE);
	if (rc < 0) {
		return rc;
	}

	/* Scan through to count frames */
	interrupt_frame = 0;
	data_frames = 0;
	cnt = 0;
	prv_cnt = UINT8_MAX;
	sample = (void *)data->fifo_data_buffer;
	for (int i = 0; i < fifo_words; i++) {
		tag = sample->tag & LSM6DSO_FIFO_TAG_SENSOR_MASK;
		cnt = sample->tag & LSM6DSO_FIFO_TAG_CNT_MASK;

		if (cnt != prv_cnt) {
			data_frames += 1;
			prv_cnt = cnt;
		}
		if (i == (data->fifo_threshold - 1)) {
			interrupt_frame = data_frames;
		}

		switch (tag) {
		case LSM6DSO_FIFO_TAG_SENSOR_GYROSCOPE_NC:
			if (gyr_out == 0) {
				/* Data frame of first gyr sample */
				gyr_out = data_frames;
			}
			samples->gyroscope.num++;
			break;
		case LSM6DSO_FIFO_TAG_SENSOR_ACCELEROMETER_NC:
			if (acc_out == 0) {
				/* Data frame of first acc sample */
				acc_out = data_frames;
			}
			samples->accelerometer.num++;
			samples->gyroscope.offset++;
			break;
		default:
			break;
		}
		sample++;
	}
	if (data_frames == 0) {
		return -ENODATA;
	}
	if (interrupt_frame == 0) {
		interrupt_frame = data_frames;
	}
	extra_frames = data_frames - interrupt_frame;

	/* Validate there is enough space for all samples */
	if ((samples->accelerometer.num + samples->gyroscope.num) > max_samples) {
		LOG_WRN("%d + %d > %d", samples->accelerometer.num, samples->gyroscope.num,
			max_samples);
		return -ENOMEM;
	}

	/* Determine real frame period */
	int_period_ticks = data->int1_timestamp - data->int1_prev_timestamp;
	frame_period_ticks = int_period_ticks / interrupt_frame;

	/* Calculate the tick count at the first and last data frame */
	first_frame_time = data->int1_prev_timestamp + frame_period_ticks;
	last_frame_time =
		data->int1_timestamp + ((extra_frames * int_period_ticks) / interrupt_frame);
	/* We want the interrupt to represent the time of the latest read data frame */
	data->int1_timestamp = last_frame_time;

	LOG_DBG("%d data frames (%d extra) at %d ticks/frame (%d us)", data_frames, extra_frames,
		frame_period_ticks, k_ticks_to_us_near32(frame_period_ticks));

	/* Calculate timestamp of first sample */
	samples->accelerometer.timestamp_ticks =
		first_frame_time + (acc_out - 1) * frame_period_ticks;
	samples->gyroscope.timestamp_ticks = first_frame_time + (gyr_out - 1) * frame_period_ticks;

	/* Store real period of samples */
	samples->accelerometer.buffer_period_ticks = (samples->accelerometer.num - 1) *
						     data->acc_time_scale * int_period_ticks /
						     interrupt_frame;
	samples->gyroscope.buffer_period_ticks = (samples->gyroscope.num - 1) *
						 data->gyr_time_scale * int_period_ticks /
						 interrupt_frame;

	/* Populate output samples */
	sample = (void *)data->fifo_data_buffer;
	gyr_out = 0;
	acc_out = 0;
	for (int i = 0; i < fifo_words; i++) {
		tag = sample->tag & LSM6DSO_FIFO_TAG_SENSOR_MASK;

		switch (tag) {
		case LSM6DSO_FIFO_TAG_SENSOR_GYROSCOPE_NC:
			samples->samples[samples->gyroscope.offset + gyr_out++] =
				*(struct imu_sample *)(&sample->x);
			break;
		case LSM6DSO_FIFO_TAG_SENSOR_ACCELEROMETER_NC:
			samples->samples[samples->accelerometer.offset + acc_out++] =
				*(struct imu_sample *)(&sample->x);
			break;
		}
		sample++;
	}

	return 0;
}

#ifdef CONFIG_INFUSE_IMU_SELF_TEST

static int lsm6dso_wait_drdy(const struct device *dev, uint8_t bit)
{
	uint8_t reg_val;
	int rc;

	for (int i = 0; i < 10; i++) {
		rc = lsm6dso_reg_read(dev, LSM6DSO_REG_STATUS_REG, &reg_val, 1);
		if (rc < 0) {
			return rc;
		}
		if (reg_val & bit) {
			return 0;
		}
		k_sleep(K_MSEC(10));
	}
	return -EAGAIN;
}

static int lsm6dso_self_test_acc(const struct device *dev)
{
	int16_t raw_base[6][3] = {0};
	int16_t raw_positive[6][3] = {0};
	int32_t avg_base[3] = {0};
	int32_t avg_positive[3] = {0};
	int16_t mg_positive[3];
	int16_t mg_base[3];
	int16_t mg_difference[3];
	uint8_t reg_val;
	int16_t one_g;
	int rc;

	LOG_DBG("Starting ACC self-test procedure");

	reg_val = LSM6DSO_CTRL1_XL_RANGE_4G | LSM6DSO_CTRL1_XL_ODR_52HZ;
	rc = lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL1_XL, &reg_val, 1);
	reg_val = 0x00;
	rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL2_G, &reg_val, 1);
	reg_val = LSM6DSO_CTRL3_C_IF_INC | LSM6DSO_CTRL3_C_BDU;
	rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL3_C, &reg_val, 1);
	reg_val = 0x00;
	rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL4_C, &reg_val, 1);
	rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL5_C, &reg_val, 1);
	rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL6_C, &reg_val, 1);
	rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL7_G, &reg_val, 1);
	rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL8_XL, &reg_val, 1);
	rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL9_XL, &reg_val, 1);
	rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL10_C, &reg_val, 1);
	if (rc != 0) {
		LOG_ERR("Failed to configure IMU for self-test mode");
		rc = -EIO;
		goto end;
	}
	k_sleep(K_MSEC(100));

	/* Read off 6 samples (first will be ignored) */
	for (int i = 0; i < 6; i++) {
		rc = lsm6dso_wait_drdy(dev, LSM6DSO_STATUS_REG_XL_DRDY);
		if (rc < 0) {
			LOG_ERR("Failed to wait for acc data-ready");
			goto end;
		}
		rc = lsm6dso_reg_read(dev, LSM6DSO_REG_OUTX_L_A, raw_base[i], 6);
		if (rc < 0) {
			LOG_ERR("Failed to read accelerometer data");
			goto end;
		}
	}

	/* Enable self-test mode */
	reg_val = LSM6DSO_CTRL5_C_SELF_TEST_XL_POS;
	rc = lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL5_C, &reg_val, 1);
	if (rc < 0) {
		LOG_ERR("Failed to enable self-test mode");
		goto end;
	}
	k_sleep(K_MSEC(100));

	/* Read off 6 samples (first will be ignored) */
	for (int i = 0; i < 6; i++) {
		rc = lsm6dso_wait_drdy(dev, LSM6DSO_STATUS_REG_XL_DRDY);
		if (rc < 0) {
			LOG_ERR("Failed to wait for acc data-ready");
			goto end;
		}
		rc = lsm6dso_reg_read(dev, LSM6DSO_REG_OUTX_L_A, raw_positive[i], 6);
		if (rc < 0) {
			LOG_ERR("Failed to read accelerometer data");
			goto end;
		}
	}

	/* Average the samples */
	for (int i = 1; i < 6; i++) {
		avg_base[0] += raw_base[i][0];
		avg_base[1] += raw_base[i][1];
		avg_base[2] += raw_base[i][2];
		avg_positive[0] += raw_positive[i][0];
		avg_positive[1] += raw_positive[i][1];
		avg_positive[2] += raw_positive[i][2];
	}
	avg_base[0] /= 5;
	avg_base[1] /= 5;
	avg_base[2] /= 5;
	avg_positive[0] /= 5;
	avg_positive[1] /= 5;
	avg_positive[2] /= 5;

	/* Convert raw register readings to milli-g */
	one_g = imu_accelerometer_1g(4);
	for (int i = 0; i < 3; i++) {
		mg_positive[i] = (1000 * avg_positive[i]) / one_g;
		mg_base[i] = (1000 * avg_base[i]) / one_g;
		mg_difference[i] = mg_positive[i] - mg_base[i];
	}

	/* Compare measured differences against specified minimums */
	if (!IN_RANGE(mg_difference[0], LSM6DSO_XL_SELF_TEST_MIN_MG, LSM6DSO_XL_SELF_TEST_MAX_MG) ||
	    !IN_RANGE(mg_difference[1], LSM6DSO_XL_SELF_TEST_MIN_MG, LSM6DSO_XL_SELF_TEST_MAX_MG) ||
	    !IN_RANGE(mg_difference[2], LSM6DSO_XL_SELF_TEST_MIN_MG, LSM6DSO_XL_SELF_TEST_MAX_MG)) {
		LOG_ERR("ACC self-test failed: X:%6d Y:%6d Z:%6d", mg_difference[0],
			mg_difference[1], mg_difference[2]);
		rc = -EINVAL;
		goto end;
	}
	LOG_DBG("Difference = X:%6d Y:%6d Z:%6d", mg_difference[0], mg_difference[1],
		mg_difference[2]);

end:
	/* Disable self-test */
	reg_val = 0x00;
	(void)lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL5_C, &reg_val, 1);
	(void)lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL1_XL, &reg_val, 1);
	return rc;
}

static int lsm6dso_self_test_gyr(const struct device *dev)
{
	int16_t raw_base[6][3] = {0};
	int16_t raw_positive[6][3] = {0};
	int32_t avg_base[3] = {0};
	int32_t avg_positive[3] = {0};
	int16_t mg_positive[3];
	int16_t mg_base[3];
	int16_t mg_difference[3];
	uint8_t reg_val;
	int rc;

	LOG_DBG("Starting GYR self-test procedure");

	reg_val = 0x00;
	rc = lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL1_XL, &reg_val, 1);
	reg_val = LSM6DSO_CTRL2_G_FS_2000DPS | LSM6DSO_CTRL2_G_ODR_208HZ;
	rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL2_G, &reg_val, 1);
	reg_val = LSM6DSO_CTRL3_C_IF_INC | LSM6DSO_CTRL3_C_BDU;
	rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL3_C, &reg_val, 1);
	reg_val = 0x00;
	rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL4_C, &reg_val, 1);
	rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL5_C, &reg_val, 1);
	rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL6_C, &reg_val, 1);
	rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL7_G, &reg_val, 1);
	rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL8_XL, &reg_val, 1);
	rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL9_XL, &reg_val, 1);
	rc |= lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL10_C, &reg_val, 1);
	if (rc != 0) {
		LOG_ERR("Failed to configure IMU for self-test mode");
		rc = -EIO;
		goto end;
	}
	k_sleep(K_MSEC(100));

	/* Read off 6 samples (first will be ignored) */
	for (int i = 0; i < 6; i++) {
		rc = lsm6dso_wait_drdy(dev, LSM6DSO_STATUS_REG_G_DRDY);
		if (rc < 0) {
			LOG_ERR("Failed to wait for acc data-ready");
			goto end;
		}
		rc = lsm6dso_reg_read(dev, LSM6DSO_REG_OUTX_L_G, raw_base[i], 6);
		if (rc < 0) {
			LOG_ERR("Failed to read accelerometer data");
			goto end;
		}
	}

	/* Enable self-test mode */
	reg_val = LSM6DSO_CTRL5_C_SELF_TEST_G_POS;
	rc = lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL5_C, &reg_val, 1);
	if (rc < 0) {
		LOG_ERR("Failed to enable self-test mode");
		goto end;
	}
	k_sleep(K_MSEC(100));

	/* Read off 6 samples (first will be ignored) */
	for (int i = 0; i < 6; i++) {
		rc = lsm6dso_wait_drdy(dev, LSM6DSO_STATUS_REG_G_DRDY);
		if (rc < 0) {
			LOG_ERR("Failed to wait for acc data-ready");
			goto end;
		}
		rc = lsm6dso_reg_read(dev, LSM6DSO_REG_OUTX_L_G, raw_positive[i], 6);
		if (rc < 0) {
			LOG_ERR("Failed to read accelerometer data");
			goto end;
		}
	}

	/* Average the samples */
	for (int i = 1; i < 6; i++) {
		avg_base[0] += raw_base[i][0];
		avg_base[1] += raw_base[i][1];
		avg_base[2] += raw_base[i][2];
		avg_positive[0] += raw_positive[i][0];
		avg_positive[1] += raw_positive[i][1];
		avg_positive[2] += raw_positive[i][2];
	}
	avg_base[0] /= 5;
	avg_base[1] /= 5;
	avg_base[2] /= 5;
	avg_positive[0] /= 5;
	avg_positive[1] /= 5;
	avg_positive[2] /= 5;

	/* Convert raw register readings to dps */
	for (int i = 0; i < 3; i++) {
		mg_positive[i] = (2000 * avg_positive[i]) / (INT16_MAX + 1);
		mg_base[i] = (2000 * avg_base[i]) / (INT16_MAX + 1);
		mg_difference[i] = mg_positive[i] - mg_base[i];
	}

	/* Compare measured differences against specified minimums */
	if (!IN_RANGE(mg_difference[0], LSM6DSO_G_SELF_TEST_MIN_DPS, LSM6DSO_G_SELF_TEST_MAX_DPS) ||
	    !IN_RANGE(mg_difference[1], LSM6DSO_G_SELF_TEST_MIN_DPS, LSM6DSO_G_SELF_TEST_MAX_DPS) ||
	    !IN_RANGE(mg_difference[2], LSM6DSO_G_SELF_TEST_MIN_DPS, LSM6DSO_G_SELF_TEST_MAX_DPS)) {
		LOG_ERR("GYR self-test failed: X:%6d Y:%6d Z:%6d", mg_difference[0],
			mg_difference[1], mg_difference[2]);
		rc = -EINVAL;
		goto end;
	}
	LOG_DBG("Difference = X:%6d Y:%6d Z:%6d", mg_difference[0], mg_difference[1],
		mg_difference[2]);

end:
	/* Disable self-test */
	reg_val = 0x00;
	(void)lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL5_C, &reg_val, 1);
	(void)lsm6dso_reg_write(dev, LSM6DSO_REG_CTRL2_G, &reg_val, 1);
	return rc;
}

/* Recommended self-test procedure from AN5192 */
static int lsm6dso_self_test(const struct device *dev)
{
	int rc;

	rc = lsm6dso_self_test_acc(dev);
	if (rc < 0) {
		goto end;
	}
	rc = lsm6dso_self_test_gyr(dev);
end:
	(void)(void)lsm6dso_low_power_reset(dev);

	return rc;
}

#endif /* CONFIG_INFUSE_IMU_SELF_TEST */

static int lsm6dso_pm_control(const struct device *dev, enum pm_device_action action)
{
	const struct lsm6dso_config *config = dev->config;
	uint8_t chip_id;
	int rc = 0;

	switch (action) {
	case PM_DEVICE_ACTION_SUSPEND:
	case PM_DEVICE_ACTION_RESUME:
	case PM_DEVICE_ACTION_TURN_OFF:
		break;
	case PM_DEVICE_ACTION_TURN_ON:
		/* Configure GPIO */
		gpio_pin_configure_dt(&config->int1_gpio, GPIO_INPUT);
		/* Registers accessible after this delay */
		k_sleep(K_MSEC(10));
		/* Initialise the bus */
		rc = lsm6dso_bus_init(dev);
		if (rc < 0) {
			LOG_ERR("Cannot communicate with IMU");
			return rc;
		}
		/* Check communications with the device */
		rc = lsm6dso_reg_read(dev, LSM6DSO_REG_WHO_AM_I, &chip_id, 1);
		if ((rc < 0) || (chip_id != LSM6DSO_WHO_AM_I)) {
			LOG_ERR("Invalid chip ID %02X", chip_id);
			return -EIO;
		}
		/* Soft reset back to low power state */
		rc = lsm6dso_low_power_reset(dev);
		break;
	default:
		return -ENOTSUP;
	}

	return rc;
}

static int lsm6dso_init(const struct device *dev)
{
	const struct lsm6dso_config *config = dev->config;
	struct lsm6dso_data *data = dev->data;

	/* Initialise data structures */
	gpio_init_callback(&data->int1_cb, lsm6dso_gpio_callback, BIT(config->int1_gpio.pin));
	/* Enable the INT1 GPIO */
	if (gpio_add_callback(config->int1_gpio.port, &data->int1_cb) < 0) {
		LOG_ERR("Could not set gpio callback");
		return -EIO;
	}
	k_sem_init(&data->int1_sem, 0, 1);

	if (lsm6dso_bus_check(dev) < 0) {
		LOG_DBG("Bus not ready");
		return -EIO;
	}

	return pm_device_driver_init(dev, lsm6dso_pm_control);
}

struct infuse_imu_api lsm6dso_imu_api = {
	.configure = lsm6dso_configure,
	.data_wait = lsm6dso_data_wait,
	.data_read = lsm6dso_data_read,
#ifdef CONFIG_INFUSE_IMU_SELF_TEST
	.self_test = lsm6dso_self_test,
#endif
};

/* Initializes a struct lsm6dso_config for an instance on a SPI bus. */
#define LSM6DSO_CONFIG_SPI(inst)                                                                   \
	.bus.spi = SPI_DT_SPEC_INST_GET(inst, SPI_WORD_SET(8) | SPI_TRANSFER_MSB),                 \
	.bus_io = &lsm6dso_bus_io_spi,

/* Initializes a struct lsm6dso_config for an instance on an I2C bus. */
#define LSM6DSO_CONFIG_I2C(inst)                                                                   \
	.bus.i2c = I2C_DT_SPEC_INST_GET(inst), .bus_io = &lsm6dso_bus_io_i2c,

#define LSM6DSO_INST(inst)                                                                         \
	static struct lsm6dso_data lsm6dso_drv_##inst;                                             \
	static const struct lsm6dso_config lsm6dso_config_##inst = {                               \
		.int1_gpio = GPIO_DT_SPEC_INST_GET_BY_IDX(inst, irq_gpios, 0),                     \
		COND_CODE_1(DT_INST_ON_BUS(inst, spi), (LSM6DSO_CONFIG_SPI(inst)),                 \
			    (LSM6DSO_CONFIG_I2C(inst)))};                                          \
	PM_DEVICE_DT_INST_DEFINE(inst, lsm6dso_pm_control);                                        \
	DEVICE_DT_INST_DEFINE(inst, lsm6dso_init, PM_DEVICE_DT_INST_GET(inst),                     \
			      &lsm6dso_drv_##inst, &lsm6dso_config_##inst, POST_KERNEL,            \
			      CONFIG_SENSOR_INIT_PRIORITY, &lsm6dso_imu_api);

DT_INST_FOREACH_STATUS_OKAY(LSM6DSO_INST);
