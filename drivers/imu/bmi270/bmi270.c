/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#define DT_DRV_COMPAT bosch_bmi270

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/drivers/imu.h>

#include "bmi270.h"

#define FIFO_BYTES MIN(BMI270_FIFO_SIZE, (7 * CONFIG_INFUSE_IMU_MAX_FIFO_SAMPLES))

struct bmi270_config {
	union bmi270_bus bus;
	const struct bmi270_bus_io *bus_io;
	struct gpio_dt_spec int1_gpio;
};

struct bmi270_data {
	struct gpio_callback int1_cb;
	struct k_sem int1_sem;
	int64_t int1_timestamp;
	int64_t int1_prev_timestamp;
	uint16_t acc_time_scale;
	uint16_t gyr_time_scale;
	uint16_t gyro_range;
	uint8_t accel_range;
	uint8_t fifo_data_buffer[FIFO_BYTES];
};

struct sensor_config {
	uint32_t period_us;
	uint8_t range;
	uint8_t config;
};

/* Global array that stores the configuration file of BMI270.
 * https://github.com/boschsensortec/BMI270_SensorAPI/blob/master/bmi270_maximum_fifo.c
 */
static const uint8_t bmi270_maximum_fifo_config_file[] = {
	0xc8, 0x2e, 0x00, 0x2e, 0x80, 0x2e, 0x1a, 0x00, 0xc8, 0x2e, 0x00, 0x2e, 0xc8, 0x2e, 0x00,
	0x2e, 0xc8, 0x2e, 0x00, 0x2e, 0xc8, 0x2e, 0x00, 0x2e, 0xc8, 0x2e, 0x00, 0x2e, 0xc8, 0x2e,
	0x00, 0x2e, 0x90, 0x32, 0x21, 0x2e, 0x59, 0xf5, 0x10, 0x30, 0x21, 0x2e, 0x6a, 0xf5, 0x1a,
	0x24, 0x22, 0x00, 0x80, 0x2e, 0x3b, 0x00, 0xc8, 0x2e, 0x44, 0x47, 0x22, 0x00, 0x37, 0x00,
	0xa4, 0x00, 0xff, 0x0f, 0xd1, 0x00, 0x07, 0xad, 0x80, 0x2e, 0x00, 0xc1, 0x80, 0x2e, 0x00,
	0xc1, 0x80, 0x2e, 0x00, 0xc1, 0x80, 0x2e, 0x00, 0xc1, 0x80, 0x2e, 0x00, 0xc1, 0x80, 0x2e,
	0x00, 0xc1, 0x80, 0x2e, 0x00, 0xc1, 0x80, 0x2e, 0x00, 0xc1, 0x80, 0x2e, 0x00, 0xc1, 0x80,
	0x2e, 0x00, 0xc1, 0x80, 0x2e, 0x00, 0xc1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x24,
	0xfc, 0xf5, 0x80, 0x30, 0x40, 0x42, 0x50, 0x50, 0x00, 0x30, 0x12, 0x24, 0xeb, 0x00, 0x03,
	0x30, 0x00, 0x2e, 0xc1, 0x86, 0x5a, 0x0e, 0xfb, 0x2f, 0x21, 0x2e, 0xfc, 0xf5, 0x13, 0x24,
	0x63, 0xf5, 0xe0, 0x3c, 0x48, 0x00, 0x22, 0x30, 0xf7, 0x80, 0xc2, 0x42, 0xe1, 0x7f, 0x3a,
	0x25, 0xfc, 0x86, 0xf0, 0x7f, 0x41, 0x33, 0x98, 0x2e, 0xc2, 0xc4, 0xd6, 0x6f, 0xf1, 0x30,
	0xf1, 0x08, 0xc4, 0x6f, 0x11, 0x24, 0xff, 0x03, 0x12, 0x24, 0x00, 0xfc, 0x61, 0x09, 0xa2,
	0x08, 0x36, 0xbe, 0x2a, 0xb9, 0x13, 0x24, 0x38, 0x00, 0x64, 0xbb, 0xd1, 0xbe, 0x94, 0x0a,
	0x71, 0x08, 0xd5, 0x42, 0x21, 0xbd, 0x91, 0xbc, 0xd2, 0x42, 0xc1, 0x42, 0x00, 0xb2, 0xfe,
	0x82, 0x05, 0x2f, 0x50, 0x30, 0x21, 0x2e, 0x21, 0xf2, 0x00, 0x2e, 0x00, 0x2e, 0xd0, 0x2e,
	0xf0, 0x6f, 0x02, 0x30, 0x02, 0x42, 0x20, 0x26, 0xe0, 0x6f, 0x02, 0x31, 0x03, 0x40, 0x9a,
	0x0a, 0x02, 0x42, 0xf0, 0x37, 0x05, 0x2e, 0x5e, 0xf7, 0x10, 0x08, 0x12, 0x24, 0x1e, 0xf2,
	0x80, 0x42, 0x83, 0x84, 0xf1, 0x7f, 0x0a, 0x25, 0x13, 0x30, 0x83, 0x42, 0x3b, 0x82, 0xf0,
	0x6f, 0x00, 0x2e, 0x00, 0x2e, 0xd0, 0x2e, 0x12, 0x40, 0x52, 0x42, 0x00, 0x2e, 0x12, 0x40,
	0x52, 0x42, 0x3e, 0x84, 0x00, 0x40, 0x40, 0x42, 0x7e, 0x82, 0xe1, 0x7f, 0xf2, 0x7f, 0x98,
	0x2e, 0x6a, 0xd6, 0x21, 0x30, 0x23, 0x2e, 0x61, 0xf5, 0xeb, 0x2c, 0xe1, 0x6f,
};

LOG_MODULE_REGISTER(bmi270, CONFIG_SENSOR_LOG_LEVEL);

static inline int bmi270_bus_check(const struct device *dev)
{
	const struct bmi270_config *cfg = dev->config;

	return cfg->bus_io->check(&cfg->bus);
}

static inline int bmi270_bus_init(const struct device *dev)
{
	const struct bmi270_config *cfg = dev->config;

	return cfg->bus_io->init(&cfg->bus);
}

static inline int bmi270_reg_read(const struct device *dev, uint8_t reg, void *data,
				  uint16_t length)
{
	const struct bmi270_config *cfg = dev->config;

	return cfg->bus_io->read(&cfg->bus, reg, data, length);
}

static inline int bmi270_reg_write(const struct device *dev, uint8_t reg, const void *data,
				   uint16_t length)
{
	const struct bmi270_config *cfg = dev->config;

	return cfg->bus_io->write(&cfg->bus, reg, data, length);
}

static int bmi270_device_init(const struct device *dev)
{
	uint8_t reg_val;
	int delay = 0, rc;

	/* Soft-reset the device */
	reg_val = BMI270_CMD_SOFTRESET;
	rc = bmi270_reg_write(dev, BMI270_REG_CMD, &reg_val, 1);
	if (rc < 0) {
		goto init_end;
	}
	k_sleep(K_USEC(BMI270_POR_DELAY));

	/* Re-initialise the bus */
	rc = bmi270_bus_init(dev);
	if (rc < 0) {
		goto init_end;
	}

	/* Disable power save mode */
	reg_val = BMI270_PWR_CONF_ADV_POWER_SAVE_DIS;
	rc = bmi270_reg_write(dev, BMI270_REG_PWR_CONF, &reg_val, 1);
	if (rc < 0) {
		goto init_end;
	}
	k_sleep(K_USEC(BMI270_PWR_CONF_DELAY));

	/* Prepare for configuration load */
	reg_val = 0x00;
	rc = bmi270_reg_write(dev, BMI270_REG_INIT_CTRL, &reg_val, 1);
	if (rc < 0) {
		goto init_end;
	}

	/* Write configuration data */
	rc = bmi270_reg_write(dev, BMI270_REG_INIT_DATA, bmi270_maximum_fifo_config_file,
			      sizeof(bmi270_maximum_fifo_config_file));
	if (rc < 0) {
		goto init_end;
	}

	/* Complete configuration load */
	reg_val = 0x01;
	rc = bmi270_reg_write(dev, BMI270_REG_INIT_CTRL, &reg_val, 1);
	if (rc < 0) {
		goto init_end;
	}

	/* Wait for configuration complete message */
	while (++delay) {
		k_sleep(K_MSEC(1));
		rc = bmi270_reg_read(dev, BMI270_REG_INTERNAL_STATUS, &reg_val, 1);
		if (rc < 0) {
			goto init_end;
		}
		if (reg_val == BMI270_INTERNAL_STATUS_INIT_OK) {
			LOG_DBG("Configuration complete after %d ms", delay);
			break;
		}
		if (delay == 20) {
			LOG_ERR("Configuration failed to load");
			rc = -EIO;
			goto init_end;
		}
	}

	/* Re-enable advanced power save mode */
	reg_val = BMI270_PWR_CONF_ADV_POWER_SAVE_EN;
	rc = bmi270_reg_write(dev, BMI270_REG_PWR_CONF, &reg_val, 1);
	if (rc < 0) {
		goto init_end;
	}

init_end:
	if (rc) {
		LOG_ERR("Cmd failed (%d)", rc);
	}
	return rc;
}

static void bmi270_gpio_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	struct bmi270_data *data = CONTAINER_OF(cb, struct bmi270_data, int1_cb);

	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	data->int1_prev_timestamp = data->int1_timestamp;
	data->int1_timestamp = k_uptime_ticks();
	k_sem_give(&data->int1_sem);
}

static int bmi270_low_power_reset(const struct device *dev)
{
	const struct bmi270_config *config = dev->config;
	struct bmi270_data *data = dev->data;
	uint8_t reg_val;
	int rc;

	(void)gpio_pin_interrupt_configure_dt(&config->int1_gpio, GPIO_INT_DISABLE);
	(void)gpio_pin_configure_dt(&config->int1_gpio, GPIO_DISCONNECTED);
	(void)k_sem_take(&data->int1_sem, K_NO_WAIT);

	reg_val = 0x00;
	rc = bmi270_reg_read(dev, BMI270_REG_PWR_CTRL, &reg_val, 1);
	if (rc == 0) {
		reg_val = BMI270_PWR_CONF_ADV_POWER_SAVE_EN;
		rc = bmi270_reg_read(dev, BMI270_REG_PWR_CONF, &reg_val, 1);
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
		ret.range = BMI270_ACC_RANGE_2G;
		break;
	case 4:
		ret.range = BMI270_ACC_RANGE_4G;
		break;
	case 8:
		ret.range = BMI270_ACC_RANGE_8G;
		break;
	case 16:
		ret.range = BMI270_ACC_RANGE_16G;
		break;
	default:
		LOG_WRN("Default range 4G");
		*fs_range = 4;
		ret.range = BMI270_ACC_RANGE_4G;
	}

	/* Sample rate selection */
	if (sample_rate == 1) {
		ret.period_us = 32 * USEC_PER_SEC / 25;
		ret.config = BMI270_ACC_CONF_ODR_25D32;
	} else if (sample_rate == 2) {
		ret.period_us = 16 * USEC_PER_SEC / 25;
		ret.config = BMI270_ACC_CONF_ODR_25D16;
	} else if (sample_rate < 5) {
		ret.period_us = 8 * USEC_PER_SEC / 25;
		ret.config = BMI270_ACC_CONF_ODR_25D8;
	} else if (sample_rate < 9) {
		ret.period_us = 4 * USEC_PER_SEC / 25;
		ret.config = BMI270_ACC_CONF_ODR_25D4;
	} else if (sample_rate < 18) {
		ret.period_us = 2 * USEC_PER_SEC / 25;
		ret.config = BMI270_ACC_CONF_ODR_25D2;
	} else if (sample_rate < 34) {
		ret.period_us = USEC_PER_SEC / 25;
		ret.config = BMI270_ACC_CONF_ODR_25;
	} else if (sample_rate < 75) {
		ret.period_us = USEC_PER_SEC / 50;
		ret.config = BMI270_ACC_CONF_ODR_50;
	} else if (sample_rate < 150) {
		ret.period_us = USEC_PER_SEC / 100;
		ret.config = BMI270_ACC_CONF_ODR_100;
	} else if (sample_rate < 300) {
		ret.period_us = USEC_PER_SEC / 200;
		ret.config = BMI270_ACC_CONF_ODR_200;
	} else if (sample_rate < 600) {
		ret.period_us = USEC_PER_SEC / 400;
		ret.config = BMI270_ACC_CONF_ODR_400;
	} else if (sample_rate < 1200) {
		ret.period_us = USEC_PER_SEC / 800;
		ret.config = BMI270_ACC_CONF_ODR_800;
	} else {
		ret.period_us = USEC_PER_SEC / 1600;
		ret.config = BMI270_ACC_CONF_ODR_1600;
	}

	/* Power configuration */
	if (low_power) {
		ret.config |= BMI270_ACC_CONF_FILTER_LOW_POWER | BMI270_ACC_CONF_LP_NO_AVG;
	} else {
		ret.config |= BMI270_ACC_CONF_FILTER_PERFORMANCE | BMI270_ACC_CONF_PERF_NORM;
	}
	return ret;
}

static struct sensor_config gyr_conf(uint16_t sample_rate, uint16_t range, bool low_power,
				     uint16_t *fs_range)
{
	struct sensor_config ret;

	/* Sensing range */
	*fs_range = range;
	switch (range) {
	case 2000:
		ret.range = BMI270_GYR_RANGE_2000DPS;
		break;
	case 1000:
		ret.range = BMI270_GYR_RANGE_1000DPS;
		break;
	case 500:
		ret.range = BMI270_GYR_RANGE_500DPS;
		break;
	case 250:
		ret.range = BMI270_GYR_RANGE_250DPS;
		break;
	case 125:
		ret.range = BMI270_GYR_RANGE_125DPS;
		break;
	default:
		LOG_WRN("Default range 1000DPS");
		*fs_range = 1000;
		ret.range = BMI270_GYR_RANGE_1000DPS;
	}

	/* Sample rate selection */
	if (sample_rate < 34) {
		ret.period_us = USEC_PER_SEC / 25;
		ret.config = BMI270_GYR_CONF_ODR_25;
	} else if (sample_rate < 75) {
		ret.period_us = USEC_PER_SEC / 50;
		ret.config = BMI270_GYR_CONF_ODR_50;
	} else if (sample_rate < 150) {
		ret.period_us = USEC_PER_SEC / 100;
		ret.config = BMI270_GYR_CONF_ODR_100;
	} else if (sample_rate < 300) {
		ret.period_us = USEC_PER_SEC / 200;
		ret.config = BMI270_GYR_CONF_ODR_200;
	} else if (sample_rate < 600) {
		ret.period_us = USEC_PER_SEC / 400;
		ret.config = BMI270_GYR_CONF_ODR_400;
	} else if (sample_rate < 1200) {
		ret.period_us = USEC_PER_SEC / 800;
		ret.config = BMI270_GYR_CONF_ODR_800;
	} else if (sample_rate < 2400) {
		ret.period_us = USEC_PER_SEC / 1600;
		ret.config = BMI270_GYR_CONF_ODR_1600;
	} else {
		ret.period_us = USEC_PER_SEC / 3200;
		ret.config = BMI270_GYR_CONF_ODR_3200;
	}
	ret.config |= BMI270_GYR_CONF_PERF_NORM;

	/* Power configuration */
	if (low_power) {
		ret.config |= BMI270_GYR_CONF_FILTER_LOW_POWER | BMI270_GYR_CONF_NOISE_LOW_POWER;
	} else {
		ret.config |=
			BMI270_GYR_CONF_FILTER_PERFORMANCE | BMI270_GYR_CONF_NOISE_PERFORMANCE;
	}
	return ret;
}

int bmi270_configure(const struct device *dev, const struct imu_config *imu_cfg,
		     struct imu_config_output *output)
{
	const struct bmi270_config *config = dev->config;
	struct bmi270_data *data = dev->data;
	struct sensor_config config_regs;
	uint32_t frame_period_us;
	uint16_t fifo_watermark;
	uint16_t samples_per_sec;
	uint8_t reg_val;
	int rc;

	/* Reset back to default state */
	rc = bmi270_low_power_reset(dev);
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
	frame_period_us = UINT32_MAX;

	/* Purge any pending FIFO data */
	reg_val = BMI270_CMD_FIFO_FLUSH;
	rc |= bmi270_reg_write(dev, BMI270_REG_CMD, &reg_val, 1);

	uint8_t fifo_config_1 = BMI270_FIFO_CONFIG_1_HEADER_EN | BMI270_FIFO_CONFIG_1_INT1_EDGE;
	uint8_t pwr_ctrl = 0x00;

	/* Configure accelerometer */
	if (imu_cfg->accelerometer.sample_rate_hz) {
		config_regs = accel_conf(imu_cfg->accelerometer.sample_rate_hz,
					 imu_cfg->accelerometer.full_scale_range,
					 imu_cfg->accelerometer.low_power, &data->accel_range);

		LOG_DBG("Acc period: %d us", config_regs.period_us);
		rc |= bmi270_reg_write(dev, BMI270_REG_ACC_CONF, &config_regs.config, 1);
		rc |= bmi270_reg_write(dev, BMI270_REG_ACC_RANGE, &config_regs.range, 1);

		output->accelerometer_period_us = config_regs.period_us;
		frame_period_us = MIN(frame_period_us, output->accelerometer_period_us);

		fifo_config_1 |= BMI270_FIFO_CONFIG_1_ACC_EN;
		pwr_ctrl |= BMI270_PWR_CTRL_ACC_EN;
	}

	/* Configure gyroscope */
	if (imu_cfg->gyroscope.sample_rate_hz) {
		config_regs = gyr_conf(imu_cfg->gyroscope.sample_rate_hz,
				       imu_cfg->gyroscope.full_scale_range,
				       imu_cfg->gyroscope.low_power, &data->gyro_range);

		LOG_DBG("Gyr period: %d us", config_regs.period_us);
		rc |= bmi270_reg_write(dev, BMI270_REG_GYR_CONF, &config_regs.config, 1);
		rc |= bmi270_reg_write(dev, BMI270_REG_GYR_RANGE, &config_regs.range, 1);

		output->gyroscope_period_us = config_regs.period_us;
		frame_period_us = MIN(frame_period_us, output->gyroscope_period_us);

		fifo_config_1 |= BMI270_FIFO_CONFIG_1_GYR_EN;
		pwr_ctrl |= BMI270_PWR_CTRL_GYR_EN;
	}

	data->acc_time_scale = output->accelerometer_period_us / frame_period_us;
	data->gyr_time_scale = output->gyroscope_period_us / frame_period_us;
	output->expected_interrupt_period_us = 0;

	/* Enable the sensors */
	reg_val = BMI270_PWR_CONF_ADV_POWER_SAVE_DIS | BMI270_PWR_CONF_FIFO_SELF_WAKE_EN;
	rc |= bmi270_reg_write(dev, BMI270_REG_PWR_CONF, &reg_val, 1);
	rc |= bmi270_reg_write(dev, BMI270_REG_PWR_CTRL, &pwr_ctrl, 1);

	/* FIFO watermark calculation.
	 * Each sample consumes 6 bytes in the FIFO.
	 * Each data frame (can contain multiple samples) consumes 1 byte in the fifo.
	 * Average headers per sample can be calculated from the sample rate ratios.
	 *   MAX(ratios) / SUM(ratios)
	 */
	fifo_watermark = 6 * imu_cfg->fifo_sample_buffer;
	fifo_watermark +=
		(MAX(data->acc_time_scale, data->gyr_time_scale) * imu_cfg->fifo_sample_buffer) /
		(data->acc_time_scale + data->gyr_time_scale);
	fifo_watermark = MIN(fifo_watermark, FIFO_BYTES - 16);
	LOG_DBG("FIFO watermark %d bytes", fifo_watermark);

	/* Approximate interrupt period */
	samples_per_sec = imu_cfg->accelerometer.sample_rate_hz + imu_cfg->gyroscope.sample_rate_hz;
	output->expected_interrupt_period_us =
		USEC_PER_SEC * imu_cfg->fifo_sample_buffer / samples_per_sec;

	/* Configure FIFO */
	rc |= bmi270_reg_write(dev, BMI270_REG_FIFO_WTM_0, &fifo_watermark, 2);
	reg_val = BMI270_INT_MAP_DATA_INT1_FIFO_WTM;
	rc |= bmi270_reg_write(dev, BMI270_REG_INT_MAP_DATA, &reg_val, 1);

	/* Enable interrupt (With INT1 edge capture) */
	reg_val = BMI270_INT1_IO_CTRL_ACTIVE_HIGH | BMI270_INT1_IO_CTRL_OUTPUT_EN |
		  BMI270_INT1_IO_CTRL_INPUT_EN | BMI270_INT1_IO_CTRL_PUSH_PULL;
	rc |= bmi270_reg_write(dev, BMI270_REG_INT1_IO_CTRL, &reg_val, 1);
	rc |= bmi270_reg_write(dev, BMI270_REG_FIFO_CONFIG_1, &fifo_config_1, 1);

	/* Approximate start time of data collection */
	data->int1_timestamp = k_uptime_ticks();

	/* Enable the INT1 GPIO */
	(void)gpio_pin_configure_dt(&config->int1_gpio, GPIO_INPUT);
	(void)gpio_pin_interrupt_configure_dt(&config->int1_gpio, GPIO_INT_EDGE_TO_ACTIVE);
	return rc ? -EIO : 0;
}

int bmi270_data_wait(const struct device *dev, k_timeout_t timeout)
{
	struct bmi270_data *data = dev->data;

	return k_sem_take(&data->int1_sem, timeout);
}

int bmi270_data_read(const struct device *dev, struct imu_sample_array *samples,
		     uint16_t max_samples)
{
	struct bmi270_data *data = dev->data;
	uint16_t fifo_length, buffer_offset = 0;
	uint16_t data_frames, extra_frames, interrupt_frame = 0;
	uint16_t gyr_out = 0, acc_out = 0;
	int64_t first_frame_time, last_frame_time;
	uint8_t fh_mode, fh_param;
	int32_t int_period_ticks;
	int32_t frame_period_ticks;
	bool extra_pending = false;
	int64_t sim_timestamp;
	uint8_t reg_val;
	int rc;

	/* Init sample output */
	samples->accelerometer = (struct imu_sensor_meta){0};
	samples->gyroscope = (struct imu_sensor_meta){0};
	samples->magnetometer = (struct imu_sensor_meta){0};

	samples->accelerometer.full_scale_range = data->accel_range;
	samples->gyroscope.full_scale_range = data->gyro_range;

	/* Get FIFO data length */
	rc = bmi270_reg_read(dev, BMI270_REG_FIFO_LENGTH_0, &fifo_length, 2);
	if (rc < 0) {
		return rc;
	}
	LOG_DBG("Reading %d bytes", fifo_length);

	/* More data pending than we have FIFO */
	if (fifo_length > sizeof(data->fifo_data_buffer)) {
		/* Round down to what we can actually fit in the buffer.
		 * Partial reads don't remove the sample from the FIFO.
		 */
		fifo_length = sizeof(data->fifo_data_buffer);
		extra_pending = true;
	}

	/* Read the FIFO data */
	rc = bmi270_reg_read(dev, BMI270_REG_FIFO_DATA, data->fifo_data_buffer, fifo_length);
	if (rc < 0) {
		return rc;
	}

	if (extra_pending) {
		/* Reset the FIFO, since handling any remaining data is questionable */
		LOG_WRN("Flushing FIFO due to overrun");
		reg_val = BMI270_CMD_FIFO_FLUSH;
		rc = bmi270_reg_write(dev, BMI270_REG_CMD, &reg_val, 1);
		if (rc < 0) {
			LOG_WRN("FIFO flush failed");
		}
		(void)k_sem_take(&data->int1_sem, K_NO_WAIT);
		sim_timestamp = k_uptime_ticks();
	}

	/* Scan through to count frames */
	data_frames = 0;
	while (buffer_offset < fifo_length) {
		/* Extract FIFO frame header params */
		fh_mode = data->fifo_data_buffer[buffer_offset] & FIFO_HEADER_MODE_MASK;
		fh_param = data->fifo_data_buffer[buffer_offset] & FIFO_HEADER_PARAM_MASK;
		if ((data_frames > 0) &&
		    (data->fifo_data_buffer[buffer_offset] & FIFO_HEADER_EXT_INT1)) {
			/* Store the data frame that triggered the interrupt */
			interrupt_frame = data_frames;
		}
		buffer_offset += 1;

		/* Handle control frames */
		if (fh_mode == FIFO_HEADER_MODE_CONTROL) {
			if (fh_param == FIFO_HEADER_CTRL_INPUT_CONFIG) {
				/* Reset state on config change.
				 * Should only happen on first few samples after configure.
				 */
				samples->accelerometer.num = 0;
				samples->gyroscope.num = 0;
				samples->gyroscope.offset = 0;
				gyr_out = 0;
				acc_out = 0;
				/* Ignore the frame */
				buffer_offset += 4;
				continue;
			} else if (fh_param == FIFO_HEADER_CTRL_SENSORTIME) {
				buffer_offset += 3;
				continue;
			} else {
				LOG_DBG("Unknown control frame %02X @ %d", fh_param,
					buffer_offset - 1);
				return -EIO;
			}
		}
		data_frames += 1;
		if (fh_param & FIFO_HEADER_REG_GYR) {
			if (gyr_out == 0) {
				/* Data frame of first gyr sample */
				gyr_out = data_frames;
			}
			samples->gyroscope.num++;
			buffer_offset += 6;
		}
		if (fh_param & FIFO_HEADER_REG_ACC) {
			if (acc_out == 0) {
				/* Data frame of first acc sample */
				acc_out = data_frames;
			}
			samples->accelerometer.num++;
			samples->gyroscope.offset++;
			buffer_offset += 6;
		}
	}
	if (data_frames == 0) {
		return -ENODATA;
	}
	if (interrupt_frame == 0) {
		interrupt_frame = data_frames;
	}
	extra_frames = data_frames - interrupt_frame;

	/* Validate there is enough space for all samples */
	if (samples->accelerometer.num + samples->gyroscope.num > max_samples) {
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
	buffer_offset = 0;
	gyr_out = 0;
	acc_out = 0;
	while (buffer_offset < fifo_length) {
		/* Extract FIFO frame header params */
		fh_mode = data->fifo_data_buffer[buffer_offset] & FIFO_HEADER_MODE_MASK;
		fh_param = data->fifo_data_buffer[buffer_offset] & FIFO_HEADER_PARAM_MASK;
		buffer_offset += 1;

		/* Control frames */
		if (fh_mode == FIFO_HEADER_MODE_CONTROL) {
			if (fh_param == FIFO_HEADER_CTRL_INPUT_CONFIG) {
				/* Reset to sample 0 in-line with first loop */
				gyr_out = 0;
				acc_out = 0;
				buffer_offset += 4;
				continue;
			} else if (fh_param == FIFO_HEADER_CTRL_SENSORTIME) {
				buffer_offset += 3;
				continue;
			}
		}

		if (fh_param & FIFO_HEADER_REG_GYR) {
			samples->samples[samples->gyroscope.offset + gyr_out++] =
				*(struct imu_sample *)(data->fifo_data_buffer + buffer_offset);
			buffer_offset += 6;
		}
		if (fh_param & FIFO_HEADER_REG_ACC) {
			samples->samples[samples->accelerometer.offset + acc_out++] =
				*(struct imu_sample *)(data->fifo_data_buffer + buffer_offset);
			buffer_offset += 6;
		}
	}

	if (extra_pending) {
		/* Set the interrupt time to the FIFO flush */
		data->int1_timestamp = sim_timestamp;
	}

	return 0;
}

static int bmi270_pm_control(const struct device *dev, enum pm_device_action action)
{
	const struct bmi270_config *config = dev->config;
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
		k_sleep(K_USEC(BMI270_POR_DELAY));
		/* Initialise the bus */
		rc = bmi270_bus_init(dev);
		if (rc < 0) {
			LOG_ERR("Cannot communicate with IMU");
			return rc;
		}
		/* Check communications with the device */
		rc = bmi270_reg_read(dev, BMI270_REG_CHIP_ID, &chip_id, 1);
		if ((rc < 0) || (chip_id != BMI270_CHIP_ID)) {
			LOG_ERR("Invalid chip ID %02X", chip_id);
			return -EIO;
		}
		/* Perform init sequence */
		rc = bmi270_device_init(dev);
		break;
	default:
		return -ENOTSUP;
	}

	return rc;
}

static int bmi270_init(const struct device *dev)
{
	const struct bmi270_config *config = dev->config;
	struct bmi270_data *data = dev->data;

	/* Initialise data structures */
	gpio_init_callback(&data->int1_cb, bmi270_gpio_callback, BIT(config->int1_gpio.pin));
	if (gpio_add_callback(config->int1_gpio.port, &data->int1_cb) < 0) {
		LOG_ERR("Could not set gpio callback");
		return -EIO;
	}
	k_sem_init(&data->int1_sem, 0, 1);

	if (bmi270_bus_check(dev) < 0) {
		LOG_DBG("Bus not ready");
		return -EIO;
	}

	return pm_device_driver_init(dev, bmi270_pm_control);
}

struct infuse_imu_api bmi270_imu_api = {
	.configure = bmi270_configure,
	.data_wait = bmi270_data_wait,
	.data_read = bmi270_data_read,
};

/* Initializes a struct bmi270_config for an instance on a SPI bus. */
#define BMI270_CONFIG_SPI(inst)                                                                    \
	.bus.spi = SPI_DT_SPEC_INST_GET(inst, SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0),              \
	.bus_io = &bmi270_bus_io_spi,

/* Initializes a struct bmi270_config for an instance on an I2C bus. */
#define BMI270_CONFIG_I2C(inst) .bus.i2c = I2C_DT_SPEC_INST_GET(inst), .bus_io = &bmi270_bus_io_i2c,

#define BMI270_INST(inst)                                                                          \
	static struct bmi270_data bmi270_drv_##inst;                                               \
	static const struct bmi270_config bmi270_config_##inst = {                                 \
		.int1_gpio = GPIO_DT_SPEC_INST_GET_BY_IDX(inst, irq_gpios, 0),                     \
		COND_CODE_1(DT_INST_ON_BUS(inst, spi), (BMI270_CONFIG_SPI(inst)),                  \
			    (BMI270_CONFIG_I2C(inst)))};                                           \
	PM_DEVICE_DT_INST_DEFINE(inst, bmi270_pm_control);                                         \
	DEVICE_DT_INST_DEFINE(inst, bmi270_init, PM_DEVICE_DT_INST_GET(inst), &bmi270_drv_##inst,  \
			      &bmi270_config_##inst, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,     \
			      &bmi270_imu_api);

DT_INST_FOREACH_STATUS_OKAY(BMI270_INST);
