/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * The ICM-20689 has no FIFO watermark interrupt, only a per-sample data ready interrupt.
 * To avoid waking on every sample, a timer emulates the watermark interrupt, expiring when
 * the requested number of frames are expected to be in the FIFO. The sample period is measured
 * from the number of frames added to the FIFO between reads.
 *
 * The interrupt pin (optional) signals FIFO overflows, so that the FIFO can be reset promptly.
 */

#define DT_DRV_COMPAT invensense_icm20689

#include <stdlib.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/drivers/imu.h>

#include "icm20689.h"

#define FIFO_BYTES MIN(ICM20689_FIFO_SIZE, (6 * CONFIG_INFUSE_IMU_MAX_FIFO_SAMPLES))

struct icm20689_config {
	struct spi_dt_spec bus;
	struct gpio_dt_spec int_gpio;
	/* INT_PIN_CFG value matching the board wiring */
	uint8_t int_pin_cfg;
};

struct icm20689_data {
	struct gpio_callback int_cb;
	struct k_timer fifo_timer;
	struct k_sem int_sem;
	/* Time the FIFO count was last read */
	int64_t fifo_read_ticks;
	/* Timestamp of the last frame returned by imu_data_read */
	int64_t last_frame_ticks;
	uint32_t frame_period_ticks;
	uint32_t nominal_period_ticks;
	uint16_t fifo_threshold;
	uint16_t fifo_remaining;
	uint16_t gyro_range;
	uint8_t accel_range;
	uint8_t frame_size;
	bool accel_en;
	bool gyro_en;
	bool timestamp_valid;
	uint8_t fifo_data_buffer[FIFO_BYTES];
};

LOG_MODULE_REGISTER(icm20689, CONFIG_SENSOR_LOG_LEVEL);

static int icm20689_reg_read(const struct device *dev, uint8_t reg, void *data, uint16_t len)
{
	const struct icm20689_config *config = dev->config;
	uint8_t addr = reg | 0x80;
	const struct spi_buf tx_buf = {.buf = &addr, .len = 1};
	const struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1};
	struct spi_buf rx_buf[2] = {
		{.buf = NULL, .len = 1},
		{.buf = data, .len = len},
	};
	const struct spi_buf_set rx = {.buffers = rx_buf, .count = ARRAY_SIZE(rx_buf)};

	return spi_transceive_dt(&config->bus, &tx, &rx);
}

static int icm20689_reg_write(const struct device *dev, uint8_t reg, uint8_t val)
{
	const struct icm20689_config *config = dev->config;
	uint8_t buf[2] = {reg & 0x7F, val};
	const struct spi_buf tx_buf = {.buf = buf, .len = sizeof(buf)};
	const struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1};

	return spi_write_dt(&config->bus, &tx);
}

static void icm20689_gpio_callback(const struct device *port, struct gpio_callback *cb,
				   uint32_t pins)
{
	struct icm20689_data *data = CONTAINER_OF(cb, struct icm20689_data, int_cb);

	ARG_UNUSED(port);
	ARG_UNUSED(pins);

	/* FIFO overflow, wake the reader to reset the FIFO */
	k_sem_give(&data->int_sem);
}

static void icm20689_timer_expiry(struct k_timer *timer)
{
	struct icm20689_data *data = CONTAINER_OF(timer, struct icm20689_data, fifo_timer);

	/* Requested number of frames should now be in the FIFO */
	k_sem_give(&data->int_sem);
}

/* Schedule the emulated watermark interrupt for when the FIFO reaches the threshold */
static void icm20689_timer_schedule(struct icm20689_data *data)
{
	uint16_t frames;

	if (data->fifo_remaining >= data->fifo_threshold) {
		k_timer_stop(&data->fifo_timer);
		k_sem_give(&data->int_sem);
		return;
	}
	frames = data->fifo_threshold - data->fifo_remaining;
	k_timer_start(&data->fifo_timer, K_TICKS((k_ticks_t)frames * data->frame_period_ticks),
		      K_FOREVER);
}

static int icm20689_low_power_reset(const struct device *dev)
{
	const struct icm20689_config *config = dev->config;
	struct icm20689_data *data = dev->data;
	uint8_t reg_val;
	int rc;

	k_timer_stop(&data->fifo_timer);
	if (config->int_gpio.port != NULL) {
		(void)gpio_pin_interrupt_configure_dt(&config->int_gpio, GPIO_INT_DISABLE);
	}
	(void)k_sem_take(&data->int_sem, K_NO_WAIT);
	data->frame_size = 0;

	/* Soft-reset the device, also selecting the best clock source (Section 4.20) */
	rc = icm20689_reg_write(dev, ICM20689_REG_PWR_MGMT_1,
				ICM20689_PWR_MGMT_1_DEVICE_RESET | ICM20689_PWR_MGMT_1_CLKSEL_AUTO);
	if (rc < 0) {
		return rc;
	}
	/* Wait for the reset to complete */
	for (int i = 0; i < 10; i++) {
		k_sleep(K_MSEC(10));
		rc = icm20689_reg_read(dev, ICM20689_REG_PWR_MGMT_1, &reg_val, 1);
		if ((rc == 0) && !(reg_val & ICM20689_PWR_MGMT_1_DEVICE_RESET)) {
			break;
		}
	}
	if (rc < 0) {
		return rc;
	}
	if (reg_val & ICM20689_PWR_MGMT_1_DEVICE_RESET) {
		return -ETIMEDOUT;
	}
	/* Prevent switching to I2C mode, then return to sleep mode */
	rc = icm20689_reg_write(dev, ICM20689_REG_USER_CTRL, ICM20689_USER_CTRL_I2C_IF_DIS);
	if (config->int_gpio.port != NULL) {
		/* Match the INT pin drive to the board, before it is ever asserted */
		rc |= icm20689_reg_write(dev, ICM20689_REG_INT_PIN_CFG, config->int_pin_cfg);
	}
	rc |= icm20689_reg_write(dev, ICM20689_REG_PWR_MGMT_1,
				 ICM20689_PWR_MGMT_1_SLEEP | ICM20689_PWR_MGMT_1_TEMP_DIS |
					 ICM20689_PWR_MGMT_1_CLKSEL_AUTO);
	return rc ? -EIO : 0;
}

/* Exit sleep mode with all sensors in standby, so that configuration registers can be written */
static int icm20689_wake_standby(const struct device *dev)
{
	int rc;

	rc = icm20689_reg_write(dev, ICM20689_REG_PWR_MGMT_2,
				ICM20689_PWR_MGMT_2_STBY_ACCEL | ICM20689_PWR_MGMT_2_STBY_GYRO);
	rc |= icm20689_reg_write(dev, ICM20689_REG_PWR_MGMT_1,
				 ICM20689_PWR_MGMT_1_TEMP_DIS | ICM20689_PWR_MGMT_1_CLKSEL_AUTO);
	if (rc != 0) {
		return -EIO;
	}
	/* Start-up time for register read/write from sleep */
	k_sleep(K_MSEC(ICM20689_REG_STARTUP_MS));
	return 0;
}

static uint8_t accel_range_config(uint8_t range, uint8_t *fs_range)
{
	*fs_range = range;
	switch (range) {
	case 2:
		return ICM20689_ACCEL_CONFIG_2G;
	case 4:
		return ICM20689_ACCEL_CONFIG_4G;
	case 8:
		return ICM20689_ACCEL_CONFIG_8G;
	case 16:
		return ICM20689_ACCEL_CONFIG_16G;
	default:
		LOG_WRN("Default range 4G");
		*fs_range = 4;
		return ICM20689_ACCEL_CONFIG_4G;
	}
}

static uint8_t gyro_range_config(uint16_t range, uint16_t *fs_range)
{
	*fs_range = range;
	switch (range) {
	case 250:
		return ICM20689_GYRO_CONFIG_250DPS;
	case 500:
		return ICM20689_GYRO_CONFIG_500DPS;
	case 1000:
		return ICM20689_GYRO_CONFIG_1000DPS;
	case 2000:
		return ICM20689_GYRO_CONFIG_2000DPS;
	default:
		LOG_WRN("Default range 2000DPS");
		*fs_range = 2000;
		return ICM20689_GYRO_CONFIG_2000DPS;
	}
}

/* Low pass filter setting with a bandwidth below the Nyquist frequency of the output rate.
 * Settings 1 to 6 have bandwidths of approximately 200, 100, 40, 20, 10, 5 Hz for both sensors.
 */
static uint8_t dlpf_config(uint16_t sample_rate)
{
	if (sample_rate >= 400) {
		return 1;
	} else if (sample_rate >= 200) {
		return 2;
	} else if (sample_rate >= 100) {
		return 3;
	} else if (sample_rate >= 50) {
		return 4;
	} else if (sample_rate >= 25) {
		return 5;
	}
	return 6;
}

static int icm20689_configure(const struct device *dev, const struct imu_config *imu_cfg,
			      struct imu_config_output *output)
{
	const struct icm20689_config *config = dev->config;
	struct icm20689_data *data = dev->data;
	uint16_t sample_rate, max_frames;
	uint32_t period_us;
	uint8_t pwr_mgmt_1, pwr_mgmt_2, accel_config2, fifo_en;
	uint8_t num_sensors, divider, dlpf;
	uint8_t acc_fs = 0;
	uint16_t gyr_fs = 0;
	bool low_power;
	int rc;

	/* Soft reset back to low power state */
	rc = icm20689_low_power_reset(dev);
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

	data->accel_en = imu_cfg->accelerometer.sample_rate_hz > 0;
	data->gyro_en = imu_cfg->gyroscope.sample_rate_hz > 0;
	num_sensors = data->accel_en + data->gyro_en;

	/* Both sensors share a single output rate */
	sample_rate = MAX(imu_cfg->accelerometer.sample_rate_hz, imu_cfg->gyroscope.sample_rate_hz);
	/* Accelerometer low power (duty-cycled) mode is limited to 500 Hz */
	low_power = !data->gyro_en && imu_cfg->accelerometer.low_power && (sample_rate <= 500);

	/* SAMPLE_RATE = INTERNAL_SAMPLE_RATE / (1 + SMPLRT_DIV) */
	divider = CLAMP((ICM20689_INTERNAL_RATE_HZ + (sample_rate / 2)) / sample_rate, 1, 256) - 1;
	if (low_power) {
		divider = MAX(divider, 1);
	}
	period_us = (USEC_PER_SEC / ICM20689_INTERNAL_RATE_HZ) * (divider + 1);
	sample_rate = ICM20689_INTERNAL_RATE_HZ / (divider + 1);
	dlpf = dlpf_config(sample_rate);

	pwr_mgmt_1 = ICM20689_PWR_MGMT_1_TEMP_DIS | ICM20689_PWR_MGMT_1_CLKSEL_AUTO;
	pwr_mgmt_2 = 0;
	accel_config2 =
		ICM20689_ACCEL_CONFIG2_FIFO_SIZE_4K | ICM20689_ACCEL_CONFIG2_A_DLPF_CFG(dlpf);
	fifo_en = 0;

	/* Configuration registers are not writable in sleep mode */
	rc = icm20689_wake_standby(dev);
	if (rc < 0) {
		return rc;
	}

	if (data->accel_en) {
		rc |= icm20689_reg_write(
			dev, ICM20689_REG_ACCEL_CONFIG,
			accel_range_config(imu_cfg->accelerometer.full_scale_range, &acc_fs));
		fifo_en |= ICM20689_FIFO_EN_ACCEL;
		if (low_power) {
			/* Average 4 samples per output, filter setting required for low power */
			pwr_mgmt_1 |= ICM20689_PWR_MGMT_1_ACCEL_CYCLE;
			pwr_mgmt_2 |= ICM20689_PWR_MGMT_2_FIFO_LP_EN;
			accel_config2 = ICM20689_ACCEL_CONFIG2_FIFO_SIZE_4K |
					ICM20689_ACCEL_CONFIG2_DEC2_CFG(0) |
					ICM20689_ACCEL_CONFIG2_A_DLPF_CFG(7);
		}
	} else {
		pwr_mgmt_2 |= ICM20689_PWR_MGMT_2_STBY_ACCEL;
	}
	if (data->gyro_en) {
		rc |= icm20689_reg_write(
			dev, ICM20689_REG_GYRO_CONFIG,
			gyro_range_config(imu_cfg->gyroscope.full_scale_range, &gyr_fs));
		fifo_en |= ICM20689_FIFO_EN_GYRO;
	} else {
		pwr_mgmt_2 |= ICM20689_PWR_MGMT_2_STBY_GYRO;
	}
	data->accel_range = acc_fs;
	data->gyro_range = gyr_fs;

	rc |= icm20689_reg_write(dev, ICM20689_REG_SMPLRT_DIV, divider);
	rc |= icm20689_reg_write(dev, ICM20689_REG_CONFIG, ICM20689_CONFIG_DLPF_CFG(dlpf));
	rc |= icm20689_reg_write(dev, ICM20689_REG_ACCEL_CONFIG2, accel_config2);
	if (config->int_gpio.port != NULL) {
		/* FIFO overflow interrupt, 50us pulse */
		rc |= icm20689_reg_write(dev, ICM20689_REG_INT_ENABLE,
					 ICM20689_INT_ENABLE_FIFO_OFLOW);
	}
	/* Wake the sensors */
	rc |= icm20689_reg_write(dev, ICM20689_REG_PWR_MGMT_2, pwr_mgmt_2);
	rc |= icm20689_reg_write(dev, ICM20689_REG_PWR_MGMT_1, pwr_mgmt_1);
	if (rc != 0) {
		return -EIO;
	}

	/* Wait for the sensors to start before buffering data */
	k_sleep(K_MSEC(data->gyro_en ? ICM20689_GYRO_STARTUP_MS : ICM20689_ACCEL_STARTUP_MS));

	/* Number of frames to buffer, limited by RAM and hardware FIFO */
	data->frame_size = 6 * num_sensors;
	max_frames = FIFO_BYTES / data->frame_size;
	data->fifo_threshold = CLAMP(imu_cfg->fifo_sample_buffer / num_sensors, 1, max_frames);
	data->fifo_remaining = 0;
	data->nominal_period_ticks = k_us_to_ticks_near32(period_us);
	data->frame_period_ticks = data->nominal_period_ticks;
	data->timestamp_valid = false;

	output->accelerometer_period_us = data->accel_en ? period_us : 0;
	output->gyroscope_period_us = data->gyro_en ? period_us : 0;
	output->magnetometer_period_us = 0;
	output->expected_interrupt_period_us = data->fifo_threshold * period_us;
	LOG_DBG("Period %d us, %d frames per interrupt", period_us, data->fifo_threshold);

	if (config->int_gpio.port != NULL) {
		(void)gpio_pin_configure_dt(&config->int_gpio, GPIO_INPUT);
		(void)gpio_pin_interrupt_configure_dt(&config->int_gpio, GPIO_INT_EDGE_TO_ACTIVE);
	}

	/* Reset and enable the FIFO */
	rc = icm20689_reg_write(dev, ICM20689_REG_USER_CTRL,
				ICM20689_USER_CTRL_I2C_IF_DIS | ICM20689_USER_CTRL_FIFO_RST);
	rc |= icm20689_reg_write(dev, ICM20689_REG_FIFO_EN, fifo_en);
	rc |= icm20689_reg_write(dev, ICM20689_REG_USER_CTRL,
				 ICM20689_USER_CTRL_I2C_IF_DIS | ICM20689_USER_CTRL_FIFO_EN);
	data->fifo_read_ticks = k_uptime_ticks();
	icm20689_timer_schedule(data);
	return rc ? -EIO : 0;
}

static int icm20689_data_wait(const struct device *dev, k_timeout_t timeout)
{
	struct icm20689_data *data = dev->data;

	return k_sem_take(&data->int_sem, timeout);
}

static void icm20689_fifo_reset(const struct device *dev)
{
	struct icm20689_data *data = dev->data;

	(void)icm20689_reg_write(dev, ICM20689_REG_USER_CTRL,
				 ICM20689_USER_CTRL_I2C_IF_DIS | ICM20689_USER_CTRL_FIFO_RST);
	(void)icm20689_reg_write(dev, ICM20689_REG_USER_CTRL,
				 ICM20689_USER_CTRL_I2C_IF_DIS | ICM20689_USER_CTRL_FIFO_EN);
	data->fifo_read_ticks = k_uptime_ticks();
	data->fifo_remaining = 0;
	data->timestamp_valid = false;
	icm20689_timer_schedule(data);
}

/* Update the frame period from the frames added to the FIFO since the last read */
static void icm20689_period_update(struct icm20689_data *data, uint16_t new_frames,
				   int64_t elapsed_ticks)
{
	uint32_t measured;

	/* Too few frames for a reliable measurement */
	if (new_frames < 4) {
		return;
	}
	measured = elapsed_ticks / new_frames;
	/* Measurement is out by up to one frame, ignore if implausible */
	if (!IN_RANGE(measured, (8 * data->nominal_period_ticks) / 10,
		      (12 * data->nominal_period_ticks) / 10)) {
		return;
	}
	/* Filter the jitter from the measurement */
	data->frame_period_ticks = (3 * data->frame_period_ticks + measured) / 4;
}

static int icm20689_data_read(const struct device *dev, struct imu_sample_array *samples,
			      uint16_t max_samples)
{
	struct icm20689_data *data = dev->data;
	uint8_t num_sensors = data->accel_en + data->gyro_en;
	int64_t read_ticks, first_frame, last_frame, expected_last;
	uint16_t fifo_count, fifo_frames, read_frames, new_frames;
	uint8_t int_status;
	uint8_t *frame;
	int rc;

	/* Init sample output */
	samples->accelerometer = (struct imu_sensor_meta){0};
	samples->gyroscope = (struct imu_sensor_meta){0};
	samples->magnetometer = (struct imu_sensor_meta){0};
	samples->accelerometer.full_scale_range = data->accel_range;
	samples->gyroscope.full_scale_range = data->gyro_range;

	if (data->frame_size == 0) {
		return -ENODATA;
	}

	/* Check for FIFO overflow, which loses the alignment of frames in the FIFO */
	rc = icm20689_reg_read(dev, ICM20689_REG_INT_STATUS, &int_status, 1);
	if (rc < 0) {
		return rc;
	}
	if (int_status & ICM20689_INT_STATUS_FIFO_OFLOW) {
		LOG_WRN("FIFO overflow");
		icm20689_fifo_reset(dev);
		return 1;
	}

	/* Get FIFO data length (reading FIFO_COUNTH latches both bytes) */
	rc = icm20689_reg_read(dev, ICM20689_REG_FIFO_COUNTH, &fifo_count, sizeof(fifo_count));
	read_ticks = k_uptime_ticks();
	if (rc < 0) {
		return rc;
	}
	fifo_count = sys_be16_to_cpu(fifo_count) & ICM20689_FIFO_COUNT_MASK;
	fifo_frames = fifo_count / data->frame_size;

	/* Frames added since the last read determine the real frame period */
	new_frames = fifo_frames - MIN(fifo_frames, data->fifo_remaining);
	icm20689_period_update(data, new_frames, read_ticks - data->fifo_read_ticks);
	data->fifo_read_ticks = read_ticks;

	/* Limit the number of frames to the RAM buffer size */
	read_frames = MIN(fifo_frames, sizeof(data->fifo_data_buffer) / data->frame_size);

	/* Validate there is enough space for all samples. The frames are discarded, as the
	 * next read would otherwise fail in the same way.
	 */
	if ((read_frames * num_sensors) > max_samples) {
		LOG_WRN("%d * %d > %d", read_frames, num_sensors, max_samples);
		icm20689_fifo_reset(dev);
		return -ENOMEM;
	}

	/* Schedule the next emulated interrupt, accounting for frames left in the FIFO */
	data->fifo_remaining = fifo_frames - read_frames;
	icm20689_timer_schedule(data);

	if (read_frames == 0) {
		return -ENODATA;
	}

	rc = icm20689_reg_read(dev, ICM20689_REG_FIFO_R_W, data->fifo_data_buffer,
			       read_frames * data->frame_size);
	if (rc < 0) {
		return rc;
	}

	/* The newest frame in the FIFO was sampled within a frame period of the FIFO count read.
	 * Timestamps continue from the previous read unless they have drifted by more than a
	 * frame period from this estimate.
	 */
	expected_last = read_ticks - (int64_t)data->fifo_remaining * data->frame_period_ticks -
			(data->frame_period_ticks / 2);
	first_frame = data->last_frame_ticks + data->frame_period_ticks;
	last_frame = first_frame + (int64_t)(read_frames - 1) * data->frame_period_ticks;
	if (!data->timestamp_valid ||
	    (llabs(last_frame - expected_last) > data->frame_period_ticks)) {
		last_frame = expected_last;
		first_frame = last_frame - (int64_t)(read_frames - 1) * data->frame_period_ticks;
	}
	data->last_frame_ticks = last_frame;
	data->timestamp_valid = true;

	LOG_DBG("%d frames (%d remaining) at %d ticks/frame", read_frames, data->fifo_remaining,
		data->frame_period_ticks);

	/* Populate sample metadata */
	if (data->accel_en) {
		samples->accelerometer.num = read_frames;
		samples->accelerometer.timestamp_ticks = first_frame;
		samples->accelerometer.buffer_period_ticks =
			(read_frames - 1) * data->frame_period_ticks;
	}
	if (data->gyro_en) {
		samples->gyroscope.offset = data->accel_en ? read_frames : 0;
		samples->gyroscope.num = read_frames;
		samples->gyroscope.timestamp_ticks = first_frame;
		samples->gyroscope.buffer_period_ticks =
			(read_frames - 1) * data->frame_period_ticks;
	}

	/* Frames contain big-endian accelerometer then gyroscope data */
	frame = data->fifo_data_buffer;
	for (int i = 0; i < read_frames; i++) {
		uint8_t *sensor = frame;

		if (data->accel_en) {
			samples->samples[samples->accelerometer.offset + i] = (struct imu_sample){
				.x = sys_get_be16(sensor + 0),
				.y = sys_get_be16(sensor + 2),
				.z = sys_get_be16(sensor + 4),
			};
			sensor += 6;
		}
		if (data->gyro_en) {
			samples->samples[samples->gyroscope.offset + i] = (struct imu_sample){
				.x = sys_get_be16(sensor + 0),
				.y = sys_get_be16(sensor + 2),
				.z = sys_get_be16(sensor + 4),
			};
		}
		frame += data->frame_size;
	}

	return 0;
}

#ifdef CONFIG_INFUSE_IMU_SELF_TEST

/* Self-test configuration: 1 kHz, ~100 Hz bandwidth, 250 dps, 2G */
#define ST_SAMPLES             64
#define ST_DLPF_CFG            2
#define ST_GYRO_LSB_DPS        131
#define ST_ACCEL_LSB_G         16384
/* Pass criteria from the InvenSense reference self-test implementation */
#define ST_GYRO_MIN_DPS        60
#define ST_GYRO_MAX_OFFSET_DPS 20
#define ST_ACCEL_MIN_MG        225
#define ST_ACCEL_MAX_MG        675

/* Average sensor outputs over a number of samples */
static int icm20689_self_test_average(const struct device *dev, int32_t acc[3], int32_t gyr[3])
{
	/* Accelerometer, temperature and gyroscope output registers */
	uint8_t raw[14];
	int rc;

	for (int i = 0; i < 3; i++) {
		acc[i] = 0;
		gyr[i] = 0;
	}
	for (int i = 0; i < ST_SAMPLES; i++) {
		rc = icm20689_reg_read(dev, ICM20689_REG_ACCEL_XOUT_H, raw, sizeof(raw));
		if (rc < 0) {
			return rc;
		}
		for (int j = 0; j < 3; j++) {
			acc[j] += (int16_t)sys_get_be16(&raw[2 * j]);
			gyr[j] += (int16_t)sys_get_be16(&raw[8 + 2 * j]);
		}
		/* Output registers update at 1 kHz */
		k_sleep(K_MSEC(2));
	}
	for (int i = 0; i < 3; i++) {
		acc[i] /= ST_SAMPLES;
		gyr[i] /= ST_SAMPLES;
	}
	return 0;
}

/* Factory self-test response from OTP code: (2620 / 2^FS) * 1.01^(code - 1), with FS = 0 */
static int32_t icm20689_self_test_factory(uint8_t code)
{
	float response = 2620.0f;

	if (code == 0) {
		return 0;
	}
	for (int i = 1; i < code; i++) {
		response *= 1.01f;
	}
	return (int32_t)response;
}

static int icm20689_self_test(const struct device *dev)
{
	int32_t acc_base[3], gyr_base[3], acc_st[3], gyr_st[3];
	uint8_t otp_gyr[3], otp_acc[3];
	int32_t response, factory;
	int rc;

	rc = icm20689_low_power_reset(dev);
	if (rc < 0) {
		return rc;
	}
	rc = icm20689_wake_standby(dev);
	if (rc < 0) {
		goto end;
	}

	/* Factory self-test codes */
	rc = icm20689_reg_read(dev, ICM20689_REG_SELF_TEST_X_GYRO, otp_gyr, sizeof(otp_gyr));
	rc |= icm20689_reg_read(dev, ICM20689_REG_SELF_TEST_X_ACCEL, otp_acc, sizeof(otp_acc));

	/* Configure sensors for self-test and enable them */
	rc |= icm20689_reg_write(dev, ICM20689_REG_SMPLRT_DIV, 0);
	rc |= icm20689_reg_write(dev, ICM20689_REG_CONFIG, ICM20689_CONFIG_DLPF_CFG(ST_DLPF_CFG));
	rc |= icm20689_reg_write(dev, ICM20689_REG_ACCEL_CONFIG2,
				 ICM20689_ACCEL_CONFIG2_A_DLPF_CFG(ST_DLPF_CFG));
	rc |= icm20689_reg_write(dev, ICM20689_REG_GYRO_CONFIG, ICM20689_GYRO_CONFIG_250DPS);
	rc |= icm20689_reg_write(dev, ICM20689_REG_ACCEL_CONFIG, ICM20689_ACCEL_CONFIG_2G);
	rc |= icm20689_reg_write(dev, ICM20689_REG_PWR_MGMT_2, 0);
	if (rc != 0) {
		LOG_ERR("Failed to configure IMU for self-test mode");
		rc = -EIO;
		goto end;
	}
	/* Wait for sensor start-up and filter settling */
	k_sleep(K_MSEC(200));

	rc = icm20689_self_test_average(dev, acc_base, gyr_base);
	if (rc < 0) {
		goto end;
	}

	/* Enable self-test on all axes */
	rc = icm20689_reg_write(dev, ICM20689_REG_GYRO_CONFIG,
				ICM20689_GYRO_CONFIG_SELF_TEST | ICM20689_GYRO_CONFIG_250DPS);
	rc |= icm20689_reg_write(dev, ICM20689_REG_ACCEL_CONFIG,
				 ICM20689_ACCEL_CONFIG_SELF_TEST | ICM20689_ACCEL_CONFIG_2G);
	if (rc != 0) {
		rc = -EIO;
		goto end;
	}
	k_sleep(K_MSEC(200));

	rc = icm20689_self_test_average(dev, acc_st, gyr_st);
	if (rc < 0) {
		goto end;
	}

	for (int i = 0; i < 3; i++) {
		/* Gyroscope response must be at least half the factory response */
		response = gyr_st[i] - gyr_base[i];
		factory = icm20689_self_test_factory(otp_gyr[i]);
		LOG_DBG("GYR %c: %d (factory %d) offset %d", 'X' + i, response, factory,
			gyr_base[i]);
		if (factory != 0 ? (2 * response <= factory)
				 : (abs(response) < ST_GYRO_MIN_DPS * ST_GYRO_LSB_DPS)) {
			LOG_ERR("GYR %c self-test response %d (factory %d)", 'X' + i, response,
				factory);
			rc = -EINVAL;
		}
		/* Gyroscope zero-rate offset */
		if (abs(gyr_base[i]) > ST_GYRO_MAX_OFFSET_DPS * ST_GYRO_LSB_DPS) {
			LOG_ERR("GYR %c offset %d", 'X' + i, gyr_base[i]);
			rc = -EINVAL;
		}

		/* Accelerometer response must be within 50% of the factory response */
		response = acc_st[i] - acc_base[i];
		factory = icm20689_self_test_factory(otp_acc[i]);
		LOG_DBG("ACC %c: %d (factory %d)", 'X' + i, response, factory);
		if (factory != 0 ? !IN_RANGE(2 * response, factory, 3 * factory)
				 : !IN_RANGE(abs(response), ST_ACCEL_MIN_MG * ST_ACCEL_LSB_G / 1000,
					     ST_ACCEL_MAX_MG * ST_ACCEL_LSB_G / 1000)) {
			LOG_ERR("ACC %c self-test response %d (factory %d)", 'X' + i, response,
				factory);
			rc = -EINVAL;
		}
	}

end:
	(void)icm20689_low_power_reset(dev);
	return rc;
}

#endif /* CONFIG_INFUSE_IMU_SELF_TEST */

static int icm20689_pm_control(const struct device *dev, enum pm_device_action action)
{
	const struct icm20689_config *config = dev->config;
	uint8_t chip_id;
	int rc = 0;

	switch (action) {
	case PM_DEVICE_ACTION_SUSPEND:
	case PM_DEVICE_ACTION_RESUME:
	case PM_DEVICE_ACTION_TURN_OFF:
		break;
	case PM_DEVICE_ACTION_TURN_ON:
		if (config->int_gpio.port != NULL) {
			(void)gpio_pin_configure_dt(&config->int_gpio, GPIO_INPUT);
		}
		/* Registers accessible after this delay */
		k_sleep(K_MSEC(11));
		/* Soft reset is required after power up (Section 4.20) */
		rc = icm20689_low_power_reset(dev);
		if (rc < 0) {
			LOG_ERR("Cannot communicate with IMU");
			return rc;
		}
		/* Check communications with the device */
		rc = icm20689_reg_read(dev, ICM20689_REG_WHO_AM_I, &chip_id, 1);
		if (rc < 0) {
			LOG_ERR("Failed to read chip ID (%d)", rc);
			return rc;
		}
		if (chip_id != ICM20689_WHO_AM_I) {
			LOG_ERR("Invalid chip ID %02X", chip_id);
			return -EIO;
		}
		break;
	default:
		return -ENOTSUP;
	}

	return rc;
}

static int icm20689_init(const struct device *dev)
{
	const struct icm20689_config *config = dev->config;
	struct icm20689_data *data = dev->data;

	k_sem_init(&data->int_sem, 0, 1);
	k_timer_init(&data->fifo_timer, icm20689_timer_expiry, NULL);

	if (!spi_is_ready_dt(&config->bus)) {
		LOG_DBG("Bus not ready");
		return -ENODEV;
	}

	if (config->int_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&config->int_gpio)) {
			return -ENODEV;
		}
		gpio_init_callback(&data->int_cb, icm20689_gpio_callback,
				   BIT(config->int_gpio.pin));
		if (gpio_add_callback(config->int_gpio.port, &data->int_cb) < 0) {
			LOG_ERR("Could not set gpio callback");
			return -EIO;
		}
	}

	return pm_device_driver_init(dev, icm20689_pm_control);
}

static const struct infuse_imu_api icm20689_imu_api = {
	.configure = icm20689_configure,
	.data_wait = icm20689_data_wait,
	.data_read = icm20689_data_read,
#ifdef CONFIG_INFUSE_IMU_SELF_TEST
	.self_test = icm20689_self_test,
#endif
};

#define ICM20689_INT_PIN_CFG(inst)                                                                 \
	(((DT_INST_GPIO_FLAGS(inst, int_gpios) & GPIO_ACTIVE_LOW) ? ICM20689_INT_PIN_CFG_LEVEL_LOW \
								  : 0) |                           \
	 (DT_INST_PROP(inst, int_open_drain) ? ICM20689_INT_PIN_CFG_OPEN_DRAIN : 0))

#define ICM20689_INST(inst)                                                                        \
	static struct icm20689_data icm20689_drv_##inst;                                           \
	static const struct icm20689_config icm20689_config_##inst = {                             \
		.bus = SPI_DT_SPEC_INST_GET(inst, SPI_WORD_SET(8) | SPI_TRANSFER_MSB),             \
		.int_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, int_gpios, {0}),                        \
		.int_pin_cfg = COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, int_gpios),                 \
					   (ICM20689_INT_PIN_CFG(inst)), (0)),                     \
	};                                                                                         \
	PM_DEVICE_DT_INST_DEFINE(inst, icm20689_pm_control);                                       \
	DEVICE_DT_INST_DEFINE(inst, icm20689_init, PM_DEVICE_DT_INST_GET(inst),                    \
			      &icm20689_drv_##inst, &icm20689_config_##inst, POST_KERNEL,          \
			      CONFIG_SENSOR_INIT_PRIORITY, &icm20689_imu_api);

DT_INST_FOREACH_STATUS_OKAY(ICM20689_INST);
