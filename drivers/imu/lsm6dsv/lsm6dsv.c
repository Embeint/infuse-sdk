/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#define DT_DRV_COMPAT st_lsm6dsv16x

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/drivers/imu.h>

#include "lsm6dsv.h"

#define FIFO_BYTES MIN(LSM6DSV_FIFO_SIZE, (7 * CONFIG_INFUSE_IMU_MAX_FIFO_SAMPLES))

struct lsm6dsv_config {
	union lsm6dsv_bus bus;
	const struct lsm6dsv_bus_io *bus_io;
	struct gpio_dt_spec int1_gpio;
};

struct lsm6dsv_data {
	struct gpio_callback int1_cb;
	struct k_sem int1_sem;
	int64_t int1_timestamp;
	int64_t int1_prev_timestamp;
	uint16_t acc_time_scale;
	uint16_t gyr_time_scale;
	uint16_t gyro_range;
	uint8_t accel_range;
	uint8_t fifo_threshold;
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
	uint8_t range;
	uint8_t config;
};

LOG_MODULE_REGISTER(lsm6dsv, CONFIG_SENSOR_LOG_LEVEL);

static inline int lsm6dsv_bus_check(const struct device *dev)
{
	const struct lsm6dsv_config *cfg = dev->config;

	return cfg->bus_io->check(&cfg->bus);
}

static inline int lsm6dsv_bus_init(const struct device *dev)
{
	const struct lsm6dsv_config *cfg = dev->config;

	return cfg->bus_io->init(&cfg->bus);
}

static inline int lsm6dsv_reg_read(const struct device *dev, uint8_t reg, void *data,
				   uint16_t length)
{
	const struct lsm6dsv_config *cfg = dev->config;

	return cfg->bus_io->read(&cfg->bus, reg, data, length);
}

static inline int lsm6dsv_reg_write(const struct device *dev, uint8_t reg, const void *data,
				    uint16_t length)
{
	const struct lsm6dsv_config *cfg = dev->config;

	return cfg->bus_io->write(&cfg->bus, reg, data, length);
}

static int lsm6dsv_low_power_reset(const struct device *dev)
{
	const struct lsm6dsv_config *cfg = dev->config;
	struct lsm6dsv_data *data = dev->data;
	uint8_t reg_val;
	int rc;

	(void)gpio_pin_interrupt_configure_dt(&cfg->int1_gpio, GPIO_INT_DISABLE);
	(void)gpio_pin_configure_dt(&cfg->int1_gpio, GPIO_DISCONNECTED);
	(void)k_sem_take(&data->int1_sem, K_NO_WAIT);

	/* Soft-reset the device */
	reg_val = LSM6DSV_FUNC_CFG_ACCESS_SW_POR;
	rc = lsm6dsv_reg_write(dev, LSM6DSV_REG_FUNC_CFG_ACCESS, &reg_val, 1);
	if (rc == 0) {
		k_sleep(K_MSEC(LSM6DSV_POR_DELAY));
	}
	return rc;
}

static struct sensor_config accel_conf(uint16_t sample_rate, uint8_t range, bool low_power,
				       uint8_t *fs_range)
{
	struct sensor_config ret;

	/* Sensing range */
	*fs_range = range;
	switch (range) {
	case 2:
		ret.range = LSM6DSV_CTRL8_ACC_RANGE_2G;
		break;
	case 4:
		ret.range = LSM6DSV_CTRL8_ACC_RANGE_4G;
		break;
	case 8:
		ret.range = LSM6DSV_CTRL8_ACC_RANGE_8G;
		break;
	case 16:
		ret.range = LSM6DSV_CTRL8_ACC_RANGE_16G;
		break;
	default:
		LOG_WRN("Default range 4G");
		*fs_range = 4;
		ret.range = LSM6DSV_CTRL8_ACC_RANGE_4G;
	}

	/* Sample rate selection */
	if (sample_rate < 4) {
		ret.period_us = 8 * USEC_PER_SEC / 15;
		ret.config = LSM6DSV_CTRL1_ACC_ODR_1HZ8;
		low_power = true;
	} else if (sample_rate < 12) {
		ret.period_us = 2 * USEC_PER_SEC / 15;
		ret.config = LSM6DSV_CTRL1_ACC_ODR_7HZ5;
	} else if (sample_rate < 23) {
		ret.period_us = USEC_PER_SEC / 15;
		ret.config = LSM6DSV_CTRL1_ACC_ODR_15HZ;
	} else if (sample_rate < 45) {
		ret.period_us = USEC_PER_SEC / 30;
		ret.config = LSM6DSV_CTRL1_ACC_ODR_30HZ;
	} else if (sample_rate < 90) {
		ret.period_us = USEC_PER_SEC / 60;
		ret.config = LSM6DSV_CTRL1_ACC_ODR_60HZ;
	} else if (sample_rate < 180) {
		ret.period_us = USEC_PER_SEC / 120;
		ret.config = LSM6DSV_CTRL1_ACC_ODR_120HZ;
	} else if (sample_rate < 300) {
		ret.period_us = USEC_PER_SEC / 240;
		ret.config = LSM6DSV_CTRL1_ACC_ODR_240HZ;
	} else if (sample_rate < 620) {
		ret.period_us = USEC_PER_SEC / 480;
		ret.config = LSM6DSV_CTRL1_ACC_ODR_480HZ;
	} else if (sample_rate < 1200) {
		ret.period_us = USEC_PER_SEC / 960;
		ret.config = LSM6DSV_CTRL1_ACC_ODR_960HZ;
	} else if (sample_rate < 2400) {
		ret.period_us = USEC_PER_SEC / 1920;
		ret.config = LSM6DSV_CTRL1_ACC_ODR_1920HZ;
	} else if (sample_rate < 4800) {
		ret.period_us = USEC_PER_SEC / 3840;
		ret.config = LSM6DSV_CTRL1_ACC_ODR_3840HZ;
	} else {
		ret.period_us = USEC_PER_SEC / 7680;
		ret.config = LSM6DSV_CTRL1_ACC_ODR_7680HZ;
	}
	if (sample_rate >= 300) {
		low_power = false;
	}

	if (low_power) {
		ret.config |= LSM6DSV_CTRL1_ACC_OP_MODE_LOW_POWER_1;
	} else {
		ret.config |= LSM6DSV_CTRL1_ACC_OP_MODE_HIGH_PERF;
	}

	return ret;
}

static struct sensor_config gyr_conf(uint16_t sample_rate, uint16_t range, bool low_power,
				     uint16_t *fs_range)
{
	struct sensor_config ret;

	*fs_range = range;
	switch (range) {
	case 4000:
		ret.range = LSM6DSV_CTRL6_GYR_RANGE_4000DPS;
		break;
	case 2000:
		ret.range = LSM6DSV_CTRL6_GYR_RANGE_2000DPS;
		break;
	case 1000:
		ret.range = LSM6DSV_CTRL6_GYR_RANGE_1000DPS;
		break;
	case 500:
		ret.range = LSM6DSV_CTRL6_GYR_RANGE_500DPS;
		break;
	case 250:
		ret.range = LSM6DSV_CTRL6_GYR_RANGE_250DPS;
		break;
	case 125:
		ret.range = LSM6DSV_CTRL6_GYR_RANGE_125DPS;
		break;
	default:
		LOG_WRN("Default range 1000DPS");
		*fs_range = 1000;
		ret.range = LSM6DSV_CTRL6_GYR_RANGE_1000DPS;
	}

	if (sample_rate < 12) {
		ret.period_us = 2 * USEC_PER_SEC / 15;
		ret.config = LSM6DSV_CTRL2_GYR_ODR_7HZ5;
	} else if (sample_rate < 23) {
		ret.period_us = USEC_PER_SEC / 15;
		ret.config = LSM6DSV_CTRL2_GYR_ODR_15HZ;
	} else if (sample_rate < 45) {
		ret.period_us = USEC_PER_SEC / 30;
		ret.config = LSM6DSV_CTRL2_GYR_ODR_30HZ;
	} else if (sample_rate < 90) {
		ret.period_us = USEC_PER_SEC / 60;
		ret.config = LSM6DSV_CTRL2_GYR_ODR_60HZ;
	} else if (sample_rate < 180) {
		ret.period_us = USEC_PER_SEC / 120;
		ret.config = LSM6DSV_CTRL2_GYR_ODR_120HZ;
	} else if (sample_rate < 300) {
		ret.period_us = USEC_PER_SEC / 240;
		ret.config = LSM6DSV_CTRL2_GYR_ODR_240HZ;
	} else if (sample_rate < 620) {
		ret.period_us = USEC_PER_SEC / 480;
		ret.config = LSM6DSV_CTRL2_GYR_ODR_480HZ;
	} else if (sample_rate < 1200) {
		ret.period_us = USEC_PER_SEC / 960;
		ret.config = LSM6DSV_CTRL2_GYR_ODR_960HZ;
	} else if (sample_rate < 2400) {
		ret.period_us = USEC_PER_SEC / 1920;
		ret.config = LSM6DSV_CTRL2_GYR_ODR_1920HZ;
	} else if (sample_rate < 4800) {
		ret.period_us = USEC_PER_SEC / 3840;
		ret.config = LSM6DSV_CTRL2_GYR_ODR_3840HZ;
	} else {
		ret.period_us = USEC_PER_SEC / 7680;
		ret.config = LSM6DSV_CTRL2_GYR_ODR_7680HZ;
	}
	if (sample_rate >= 300) {
		low_power = false;
	}

	if (low_power) {
		ret.config |= LSM6DSV_CTRL2_GYR_OP_MODE_LOW_POWER;
	} else {
		ret.config |= LSM6DSV_CTRL2_GYR_OP_MODE_HIGH_PERF;
	}
	return ret;
}

static void lsm6dsv_gpio_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	struct lsm6dsv_data *data = CONTAINER_OF(cb, struct lsm6dsv_data, int1_cb);

	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	data->int1_prev_timestamp = data->int1_timestamp;
	data->int1_timestamp = k_uptime_ticks();
	LOG_DBG("");
	k_sem_give(&data->int1_sem);
}

int lsm6dsv_configure(const struct device *dev, const struct imu_config *imu_cfg,
		      struct imu_config_output *output)
{
	const struct lsm6dsv_config *config = dev->config;
	struct lsm6dsv_data *data = dev->data;
	struct sensor_config config_regs;
	uint32_t frame_period_us;
	uint8_t reg_val, fifo_ctrl3 = 0;
	int rc;

	/* Soft reset back to low power state */
	rc = lsm6dsv_low_power_reset(dev);
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
		config_regs = accel_conf(imu_cfg->accelerometer.sample_rate_hz,
					 imu_cfg->accelerometer.full_scale_range,
					 imu_cfg->accelerometer.low_power, &data->accel_range);

		LOG_DBG("Acc period: %d us", config_regs.period_us);
		rc |= lsm6dsv_reg_write(dev, LSM6DSV_REG_CTRL1, &config_regs.config, 1);
		rc |= lsm6dsv_reg_write(dev, LSM6DSV_REG_CTRL8, &config_regs.range, 1);

		fifo_ctrl3 |= (config_regs.config & 0x0F);
		output->accelerometer_period_us = config_regs.period_us;
		frame_period_us = MIN(frame_period_us, output->accelerometer_period_us);
	}

	/* Configure gyroscope */
	if (imu_cfg->gyroscope.sample_rate_hz) {
		config_regs = gyr_conf(imu_cfg->gyroscope.sample_rate_hz,
				       imu_cfg->gyroscope.full_scale_range,
				       imu_cfg->gyroscope.low_power, &data->gyro_range);

		LOG_DBG("Gyr period: %d us", config_regs.period_us);
		rc |= lsm6dsv_reg_write(dev, LSM6DSV_REG_CTRL2, &config_regs.config, 1);
		rc |= lsm6dsv_reg_write(dev, LSM6DSV_REG_CTRL6, &config_regs.range, 1);

		fifo_ctrl3 |= (config_regs.config & 0x0F) << 4;
		output->gyroscope_period_us = config_regs.period_us;
		frame_period_us = MIN(frame_period_us, output->gyroscope_period_us);
	}
	/* Relative ratio of accelerometer and gyroscope samples */
	data->acc_time_scale = output->accelerometer_period_us / frame_period_us;
	data->gyr_time_scale = output->gyroscope_period_us / frame_period_us;

	/* Watermark threshold limited to 8 bits */
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
	rc |= lsm6dsv_reg_write(dev, LSM6DSV_REG_FIFO_CTRL1, &data->fifo_threshold, 1);
	rc |= lsm6dsv_reg_write(dev, LSM6DSV_REG_FIFO_CTRL3, &fifo_ctrl3, 1);
	reg_val = LSM6DSV_FIFO_CTRL4_MODE_FIFO;
	rc |= lsm6dsv_reg_write(dev, LSM6DSV_REG_FIFO_CTRL4, &reg_val, 1);

	/* Configure INT1 for FIFO threshold */
	reg_val = LSM6DSV_INT1_CTRL_FIFO_THR;
	rc |= lsm6dsv_reg_write(dev, LSM6DSV_REG_INT1_CTRL, &reg_val, 1);

	/* Approximate start time of data collection */
	data->int1_timestamp = k_uptime_ticks();

	/* Enable the interrupt GPIO */
	(void)gpio_pin_configure_dt(&config->int1_gpio, GPIO_INPUT);
	(void)gpio_pin_interrupt_configure_dt(&config->int1_gpio, GPIO_INT_EDGE_TO_ACTIVE);
	return rc ? -EIO : 0;
}

int lsm6dsv_data_wait(const struct device *dev, k_timeout_t timeout)
{
	struct lsm6dsv_data *data = dev->data;

	return k_sem_take(&data->int1_sem, timeout);
}

int lsm6dsv_data_read(const struct device *dev, struct imu_sample_array *samples,
		      uint16_t max_samples)
{
	const struct lsm6dsv_config *config = dev->config;
	struct lsm6dsv_data *data = dev->data;
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
	rc = lsm6dsv_reg_read(dev, LSM6DSV_REG_FIFO_STATUS1, &fifo_status, sizeof(fifo_status));
	if (rc < 0) {
		return rc;
	}

	/* Limit the number of words that can be read out to our RAM buffer size */
	fifo_words = fifo_status & 0x1FF;
	if ((7 * fifo_words) > sizeof(data->fifo_data_buffer)) {
		fifo_words = sizeof(data->fifo_data_buffer) / 7;
	}
	LOG_DBG("Reading %04X %d bytes", fifo_status, 7 * fifo_words);

	/* A "false" interrupt can be generated while reading the FIFO if another sample is added to
	 * the FIFO as we empty the FIFO past the configured threshold.
	 * Disable the interrupt around the read operation to prevent this from happening.
	 */

	(void)gpio_pin_interrupt_configure_dt(&config->int1_gpio, GPIO_INT_DISABLE);
	rc = lsm6dsv_reg_read(dev, LSM6DSV_REG_FIFO_DATA_OUT_TAG, data->fifo_data_buffer,
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
		tag = sample->tag & LSM6DSV_FIFO_TAG_SENSOR_MASK;
		cnt = (sample->tag & 0b110) >> 1;

		if (cnt != prv_cnt) {
			data_frames += 1;
			prv_cnt = cnt;
		}
		if (i == (data->fifo_threshold - 1)) {
			interrupt_frame = data_frames;
		}

		switch (tag) {
		case LSM6DSV_FIFO_TAG_SENSOR_GYROSCOPE_NC:
			if (gyr_out == 0) {
				/* Data frame of first gyr sample */
				gyr_out = data_frames;
			}
			samples->gyroscope.num++;
			break;
		case LSM6DSV_FIFO_TAG_SENSOR_ACCELEROMETER_NC:
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
		tag = sample->tag & LSM6DSV_FIFO_TAG_SENSOR_MASK;

		switch (tag) {
		case LSM6DSV_FIFO_TAG_SENSOR_GYROSCOPE_NC:
			samples->samples[samples->gyroscope.offset + gyr_out++] =
				*(struct imu_sample *)(&sample->x);
			break;
		case LSM6DSV_FIFO_TAG_SENSOR_ACCELEROMETER_NC:
			samples->samples[samples->accelerometer.offset + acc_out++] =
				*(struct imu_sample *)(&sample->x);
			break;
		}
		sample++;
	}

	return 0;
}

static int lsm6dsv_pm_control(const struct device *dev, enum pm_device_action action)
{
	const struct lsm6dsv_config *config = dev->config;
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
		rc = lsm6dsv_bus_init(dev);
		if (rc < 0) {
			LOG_ERR("Cannot communicate with IMU");
			return rc;
		}
		/* Check communications with the device */
		rc = lsm6dsv_reg_read(dev, LSM6DSV_REG_WHO_AM_I, &chip_id, 1);
		if ((rc < 0) || (chip_id != LSM6DSV_WHO_AM_I)) {
			LOG_ERR("Invalid chip ID %02X", chip_id);
			return -EIO;
		}
		/* Soft reset back to low power state */
		rc = lsm6dsv_low_power_reset(dev);
		break;
	default:
		return -ENOTSUP;
	}

	return rc;
}

static int lsm6dsv_init(const struct device *dev)
{
	const struct lsm6dsv_config *config = dev->config;
	struct lsm6dsv_data *data = dev->data;

	/* Initialise data structures */
	gpio_init_callback(&data->int1_cb, lsm6dsv_gpio_callback, BIT(config->int1_gpio.pin));
	/* Enable the INT1 GPIO */
	if (gpio_add_callback(config->int1_gpio.port, &data->int1_cb) < 0) {
		LOG_ERR("Could not set gpio callback");
		return -EIO;
	}
	k_sem_init(&data->int1_sem, 0, 1);

	if (lsm6dsv_bus_check(dev) < 0) {
		LOG_DBG("Bus not ready");
		return -EIO;
	}

	return pm_device_driver_init(dev, lsm6dsv_pm_control);
}

struct infuse_imu_api lsm6dsv_imu_api = {
	.configure = lsm6dsv_configure,
	.data_wait = lsm6dsv_data_wait,
	.data_read = lsm6dsv_data_read,
};

/* Initializes a struct lsm6dsv_config for an instance on a SPI bus. */
#define LSM6DSV_CONFIG_SPI(inst)                                                                   \
	.bus.spi = SPI_DT_SPEC_INST_GET(inst, SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0),              \
	.bus_io = &lsm6dsv_bus_io_spi,

/* Initializes a struct lsm6dsv_config for an instance on an I2C bus. */
#define LSM6DSV_CONFIG_I2C(inst)                                                                   \
	.bus.i2c = I2C_DT_SPEC_INST_GET(inst), .bus_io = &lsm6dsv_bus_io_i2c,

#define LSM6DSV_INST(inst)                                                                         \
	static struct lsm6dsv_data lsm6dsv_drv_##inst;                                             \
	static const struct lsm6dsv_config lsm6dsv_config_##inst = {                               \
		.int1_gpio = GPIO_DT_SPEC_INST_GET_BY_IDX(inst, int1_gpios, 0),                    \
		COND_CODE_1(DT_INST_ON_BUS(inst, spi), (LSM6DSV_CONFIG_SPI(inst)),                 \
			    (LSM6DSV_CONFIG_I2C(inst)))};                                          \
	PM_DEVICE_DT_INST_DEFINE(inst, lsm6dsv_pm_control);                                        \
	DEVICE_DT_INST_DEFINE(inst, lsm6dsv_init, PM_DEVICE_DT_INST_GET(inst),                     \
			      &lsm6dsv_drv_##inst, &lsm6dsv_config_##inst, POST_KERNEL,            \
			      CONFIG_SENSOR_INIT_PRIORITY, &lsm6dsv_imu_api);

DT_INST_FOREACH_STATUS_OKAY(LSM6DSV_INST);
