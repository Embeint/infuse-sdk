/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#define DT_DRV_COMPAT st_lis2dw12

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/drivers/imu.h>

#include "lis2dw12.h"

struct lis2dw12_config {
	union lis2dw12_bus bus;
	const struct lis2dw12_bus_io *bus_io;
	struct gpio_dt_spec irq_gpio;
	uint8_t lp_mode;
	uint8_t ctrl6_base;
};

struct lis2dw12_data {
	struct gpio_callback int_cb;
	struct k_sem int_sem;
	int64_t int_timestamp;
	int64_t int_prev_timestamp;
	uint16_t acc_time_scale;
	uint8_t accel_range;
	uint8_t fifo_threshold;
	struct lis2dw12_fifo_frame fifo_data_buffer[LIS2DW12_FIFO_FRAME_SIZE];
};

struct sensor_config {
	uint32_t period_us;
	uint8_t ctrl1;
	uint8_t ctrl6;
};

LOG_MODULE_REGISTER(lis2dw12, CONFIG_SENSOR_LOG_LEVEL);

static inline int lis2dw12_bus_check(const struct device *dev)
{
	const struct lis2dw12_config *cfg = dev->config;

	return cfg->bus_io->check(&cfg->bus);
}

static inline int lis2dw12_bus_init(const struct device *dev)
{
	const struct lis2dw12_config *cfg = dev->config;

	return cfg->bus_io->init(&cfg->bus);
}

static inline int lis2dw12_reg_read(const struct device *dev, uint8_t reg, void *data,
				    uint16_t length)
{
	const struct lis2dw12_config *cfg = dev->config;

	return cfg->bus_io->read(&cfg->bus, reg, data, length);
}

static inline int lis2dw12_reg_write(const struct device *dev, uint8_t reg, const void *data,
				     uint16_t length)
{
	const struct lis2dw12_config *cfg = dev->config;

	return cfg->bus_io->write(&cfg->bus, reg, data, length);
}

static int lis2dw12_trim_reset(const struct device *dev)
{
	uint8_t trim_reset = LIS2DW_CTRL2_BOOT | LIS2DW_CTRL2_IF_ADD_INC;
	int rc;

	/* Write BOOT bit to 1 to force the trim reload */
	rc = lis2dw12_reg_write(dev, LIS2DW12_REG_CTRL2, &trim_reset, 1);
	if (rc < 0) {
		return rc;
	}
	/* Wait for BOOT bit to clear (typically immediately) */
	for (int i = 0; i < 5; i++) {
		rc = lis2dw12_reg_read(dev, LIS2DW12_REG_CTRL2, &trim_reset, 1);
		if (rc < 0) {
			return rc;
		}
		if (!(trim_reset & LIS2DW_CTRL2_BOOT)) {
			return 0;
		}
		k_sleep(K_MSEC(1));
	}
	return -ETIMEDOUT;
}

static int lis2dw12_low_power_reset(const struct device *dev)
{
	const struct lis2dw12_config *config = dev->config;
	uint8_t reg, soft_reset = LIS2DW_CTRL2_SOFT_RESET;
	int rc = 0;

	/* Disable the IRQ GPIO */
	(void)gpio_pin_interrupt_configure_dt(&config->irq_gpio, GPIO_INT_DISABLE);

	rc = lis2dw12_reg_write(dev, LIS2DW12_REG_CTRL2, &soft_reset, 1);
	if (rc < 0) {
		return rc;
	}
	/* Wait for SOFT_RESET bit to clear (typically immediately) */
	for (int i = 0; i < 5; i++) {
		rc = lis2dw12_reg_read(dev, LIS2DW12_REG_CTRL2, &soft_reset, 1);
		if (rc < 0) {
			return rc;
		}
		if (!(soft_reset & LIS2DW_CTRL2_SOFT_RESET)) {
			goto set_add_inc;
		}
		k_sleep(K_MSEC(1));
	}
	return -ETIMEDOUT;
set_add_inc:
	reg = LIS2DW_CTRL2_IF_ADD_INC;
	return lis2dw12_reg_write(dev, LIS2DW12_REG_CTRL2, &reg, 1);
}

static void lis2dw12_gpio_callback(const struct device *dev, struct gpio_callback *cb,
				   uint32_t pins)
{
	struct lis2dw12_data *data = CONTAINER_OF(cb, struct lis2dw12_data, int_cb);

	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	data->int_prev_timestamp = data->int_timestamp;
	data->int_timestamp = k_uptime_ticks();
	LOG_DBG("");
	k_sem_give(&data->int_sem);
}

static struct sensor_config accel_conf(const struct device *dev, uint16_t sample_rate,
				       uint8_t range, bool low_power, uint8_t *fs_range)
{
	const struct lis2dw12_config *config = dev->config;
	struct sensor_config ret;

	/* Sensing range */
	*fs_range = range;
	switch (range) {
	case 2:
		ret.ctrl6 = LIS2DW_CTRL6_FS_2G;
		break;
	case 4:
		ret.ctrl6 = LIS2DW_CTRL6_FS_4G;
		break;
	case 8:
		ret.ctrl6 = LIS2DW_CTRL6_FS_8G;
		break;
	case 16:
		ret.ctrl6 = LIS2DW_CTRL6_FS_16G;
		break;
	default:
		LOG_WRN("Default range 4G");
		*fs_range = 4;
		ret.ctrl6 = LIS2DW_CTRL6_FS_4G;
	}
	ret.ctrl6 |= config->ctrl6_base;

	/* Sample rate selection */
	if (sample_rate < 6) {
		ret.period_us = 16 * USEC_PER_SEC / 25;
		ret.ctrl1 = LIS2DW_CTRL1_ODR_12HZ5_1HZ6;
		low_power = true;
	} else if (sample_rate < 19) {
		ret.period_us = 2 * USEC_PER_SEC / 25;
		ret.ctrl1 = LIS2DW_CTRL1_ODR_12HZ5;
	} else if (sample_rate < 37) {
		ret.period_us = USEC_PER_SEC / 25;
		ret.ctrl1 = LIS2DW_CTRL1_ODR_25HZ;
	} else if (sample_rate < 75) {
		ret.period_us = USEC_PER_SEC / 50;
		ret.ctrl1 = LIS2DW_CTRL1_ODR_50HZ;
	} else if (sample_rate < 150) {
		ret.period_us = USEC_PER_SEC / 100;
		ret.ctrl1 = LIS2DW_CTRL1_ODR_100HZ;
	} else if (sample_rate < 300) {
		ret.period_us = USEC_PER_SEC / 200;
		ret.ctrl1 = LIS2DW_CTRL1_ODR_200HZ;
	} else if (sample_rate < 600) {
		ret.period_us = USEC_PER_SEC / 400;
		ret.ctrl1 = LIS2DW_CTRL1_ODR_400HZ;
	} else if (sample_rate < 1200) {
		ret.period_us = USEC_PER_SEC / 800;
		ret.ctrl1 = LIS2DW_CTRL1_ODR_800HZ;
	} else {
		ret.period_us = USEC_PER_SEC / 1600;
		ret.ctrl1 = LIS2DW_CTRL1_ODR_1600HZ;
	}
	if (sample_rate >= 300) {
		low_power = false;
	}

	if (low_power) {
		ret.ctrl1 |= LIS2DW_CTRL1_MODE_LOW_POWER | LIS2DW_CTRL1_MODE_LPM4;
	} else {
		ret.ctrl1 |= LIS2DW_CTRL1_MODE_HIGH_PERFORMANCE;
	}
	ret.ctrl1 |= config->lp_mode;

	return ret;
}

int lis2dw12_configure(const struct device *dev, const struct imu_config *imu_cfg,
		       struct imu_config_output *output)
{
	const struct lis2dw12_config *config = dev->config;
	struct lis2dw12_data *data = dev->data;
	struct sensor_config config_regs;
	uint8_t reg;
	int rc = 0;

	/* Soft reset back to low power state */
	rc = lis2dw12_low_power_reset(dev);
	if (rc < 0) {
		return rc;
	}

	/* No more work to do */
	if (imu_cfg == NULL) {
		return 0;
	}
	if (imu_cfg->accelerometer.sample_rate_hz == 0) {
		if (imu_cfg->gyroscope.sample_rate_hz || imu_cfg->magnetometer.sample_rate_hz) {
			/* Only gyro or mag requested */
			rc = -ENOTSUP;
		}
		return rc;
	}
	if (imu_cfg->fifo_sample_buffer == 0) {
		return -EINVAL;
	}

	output->accelerometer_period_us = 0;
	output->gyroscope_period_us = 0;
	output->magnetometer_period_us = 0;
	output->expected_interrupt_period_us = 0;

	/* Limit the FIFO threshold to 1 less than the maximum value to give driver users a chance
	 * to read data before the FIFO is full and we lose all knowledge of how many FIFO frames
	 * were dropped.
	 */
	data->fifo_threshold = MIN((LIS2DW12_FIFO_FRAME_SIZE - 1), imu_cfg->fifo_sample_buffer);

	config_regs = accel_conf(dev, imu_cfg->accelerometer.sample_rate_hz,
				 imu_cfg->accelerometer.full_scale_range,
				 imu_cfg->accelerometer.low_power, &data->accel_range);
	output->accelerometer_period_us = config_regs.period_us;
	output->expected_interrupt_period_us =
		data->fifo_threshold * output->accelerometer_period_us;

	/* FIFO threshold value and interrupt routing */
	reg = LIS2DW_FIFO_CTRL_MODE_CONTINUOUS | data->fifo_threshold;
	rc = lis2dw12_reg_write(dev, LIS2DW12_REG_FIFO_CTRL, &reg, 1);
	reg = LIS2DW_CTRL4_INT1_FTH;
	rc |= lis2dw12_reg_write(dev, LIS2DW12_REG_CTRL4_INT1_PAD, &reg, 1);
	reg = LIS2DW_CTRL7_INTERRUPTS_ENABLE;
	rc |= lis2dw12_reg_write(dev, LIS2DW12_REG_CTRL7, &reg, 1);

	/* Configure accelerometer */
	rc |= lis2dw12_reg_write(dev, LIS2DW12_REG_CTRL6, &config_regs.ctrl6, 1);
	rc |= lis2dw12_reg_write(dev, LIS2DW12_REG_CTRL1, &config_regs.ctrl1, 1);

	/* Approximate start time of data collection */
	data->int_timestamp = k_uptime_ticks();

	/* Enable the IRQ GPIO */
	(void)gpio_pin_interrupt_configure_dt(&config->irq_gpio, GPIO_INT_EDGE_TO_ACTIVE);
	if (rc) {
		rc = -EIO;
	}
	return 0;
}

int lis2dw12_data_wait(const struct device *dev, k_timeout_t timeout)
{
	struct lis2dw12_data *data = dev->data;

	return k_sem_take(&data->int_sem, timeout);
}

int lis2dw12_data_read(const struct device *dev, struct imu_sample_array *samples,
		       uint16_t max_samples)
{
	struct lis2dw12_data *data = dev->data;
	int64_t first_frame_time, last_frame_time;
	uint8_t extra_frames, acc_samples, fifo_samples;
	int32_t int_period_ticks;
	int32_t frame_period_ticks;
	int rc;

	/* Init sample output */
	samples->accelerometer = (struct imu_sensor_meta){0};
	samples->gyroscope = (struct imu_sensor_meta){0};
	samples->magnetometer = (struct imu_sensor_meta){0};

	/* Query number of samples pending */
	rc = lis2dw12_reg_read(dev, LIS2DW12_REG_FIFO_SAMPLES, &fifo_samples, 1);
	if (rc < 0) {
		return rc;
	}

	/* Read the FIFO data */
	acc_samples = fifo_samples & LIS2DW_FIFO_SAMPLES_COUNT_MASK;
	LOG_DBG("Reading %d samples", acc_samples);

	rc = lis2dw12_reg_read(dev, LIS2DW12_REG_OUT_X_L, data->fifo_data_buffer,
			       acc_samples * sizeof(struct lis2dw12_fifo_frame));
	if (rc < 0) {
		return rc;
	}
	extra_frames = acc_samples - data->fifo_threshold;

	/* Validate there is enough space for samples */
	if (acc_samples > max_samples) {
		LOG_WRN("%d > %d", acc_samples, max_samples);
		return -ENOMEM;
	}

	/* Determine real frame period */
	int_period_ticks = data->int_timestamp - data->int_prev_timestamp;
	frame_period_ticks = int_period_ticks / data->fifo_threshold;

	/* Calculate the tick count at the first and last data frame */
	first_frame_time = data->int_prev_timestamp + frame_period_ticks;
	last_frame_time =
		data->int_timestamp + ((extra_frames * int_period_ticks) / data->fifo_threshold);
	/* We want the interrupt to represent the time of the latest read data frame */
	data->int_timestamp = last_frame_time;
	/* FIFO frames may have been dropped, check FIFO_OVR flag */
	if (fifo_samples & LIS2DW_FIFO_SAMPLES_FIFO_OVR) {
		LOG_DBG("FIFO overrun");
		/* We have no idea how many samples have been dropped. Use the current time */
		data->int_timestamp = k_uptime_ticks();
		/* Return overrun status */
		rc = 1;
	}

	LOG_DBG("%d data frames (%d extra) at %d ticks/frame (%d us)", acc_samples, extra_frames,
		frame_period_ticks, k_ticks_to_us_near32(frame_period_ticks));

	/* Store output parameters */
	samples->accelerometer.num = acc_samples;
	samples->accelerometer.full_scale_range = data->accel_range;
	samples->accelerometer.timestamp_ticks = first_frame_time;
	samples->accelerometer.buffer_period_ticks =
		(acc_samples - 1) * int_period_ticks / data->fifo_threshold;

	for (int i = 0; i < acc_samples; i++) {
		samples->samples[i].x = data->fifo_data_buffer[i].x;
		samples->samples[i].y = data->fifo_data_buffer[i].y;
		samples->samples[i].z = data->fifo_data_buffer[i].z;
	}
	return rc;
}

#ifdef CONFIG_INFUSE_IMU_SELF_TEST

static int wait_data_ready(const struct device *dev)
{
	uint8_t reg;
	int rc;

	for (int i = 0; i < 100; i++) {
		rc = lis2dw12_reg_read(dev, LIS2DW12_REG_STATUS, &reg, 1);
		if (rc < 0) {
			return rc;
		}
		if (reg & LIS2DW_STATUS_DRDY) {
			return 0;
		}
		k_sleep(K_MSEC(1));
	}
	return -ETIMEDOUT;
}

static int run_test_sequence(const struct device *dev, struct lis2dw12_fifo_frame *average)
{
	struct lis2dw12_fifo_frame frame;
	int32_t sum_x = 0, sum_y = 0, sum_z = 0;
	int16_t one_g;
	int rc;

	/* Wait for 100ms - stabilize output */
	k_sleep(K_MSEC(100));

	/* Average 5 samples, discarding the first */
	for (int i = 0; i < 6; i++) {
		rc = wait_data_ready(dev);
		if (rc < 0) {
			return rc;
		}
		rc = lis2dw12_reg_read(dev, LIS2DW12_REG_OUT_X_L, &frame,
				       sizeof(struct lis2dw12_fifo_frame));
		if (rc < 0) {
			return rc;
		}
		if (i == 0) {
			continue;
		}

		sum_x += frame.x;
		sum_y += frame.y;
		sum_z += frame.z;
	}

	/* Output in milli-gs */
	one_g = imu_accelerometer_1g(4);
	average->x = (1000 * sum_x / 5) / one_g;
	average->y = (1000 * sum_y / 5) / one_g;
	average->z = (1000 * sum_z / 5) / one_g;
	return 0;
}

/* Recommended self-test procedure from DT0127 */
static int lis2dw12_self_test(const struct device *dev)
{
	struct lis2dw12_fifo_frame base_mg, pos_mg, diff_mg;
	uint8_t reg;
	int rc;

	LOG_DBG("Starting self-test procedure");

	/* Soft reset back to low power state */
	rc = lis2dw12_low_power_reset(dev);
	if (rc < 0) {
		return rc;
	}

	/* BDU = 1; FS = 4g; ODR = 50Hz – High-performance mode */
	reg = LIS2DW_CTRL2_BDU | LIS2DW_CTRL2_IF_ADD_INC;
	rc |= lis2dw12_reg_write(dev, LIS2DW12_REG_CTRL2, &reg, 1);
	reg = LIS2DW_CTRL6_FS_4G;
	rc |= lis2dw12_reg_write(dev, LIS2DW12_REG_CTRL6, &reg, 1);
	reg = LIS2DW_CTRL1_ODR_50HZ | LIS2DW_CTRL1_MODE_HIGH_PERFORMANCE;
	rc |= lis2dw12_reg_write(dev, LIS2DW12_REG_CTRL1, &reg, 1);
	if (rc < 0) {
		return -EIO;
	}

	/* Run the base test case */
	rc = run_test_sequence(dev, &base_mg);
	if (rc < 0) {
		return -EIO;
	}

	/* Enable positive sign self-test */
	reg = LIS2DW_CTRL3_SELF_TEST_POSITIVE;
	rc = lis2dw12_reg_write(dev, LIS2DW12_REG_CTRL3, &reg, 1);

	/* Run the positive excitation case  */
	rc = run_test_sequence(dev, &pos_mg);
	if (rc < 0) {
		return -EIO;
	}

	diff_mg.x = pos_mg.x - base_mg.x;
	diff_mg.y = pos_mg.y - base_mg.y;
	diff_mg.z = pos_mg.z - base_mg.z;

	/* Compare measured differences against specified range */
	if (!IN_RANGE(diff_mg.x, LIS2DW12_SELF_TEST_DEFLECTION_MIN,
		      LIS2DW12_SELF_TEST_DEFLECTION_MAX) ||
	    !IN_RANGE(diff_mg.y, LIS2DW12_SELF_TEST_DEFLECTION_MIN,
		      LIS2DW12_SELF_TEST_DEFLECTION_MAX) ||
	    !IN_RANGE(diff_mg.z, LIS2DW12_SELF_TEST_DEFLECTION_MIN,
		      LIS2DW12_SELF_TEST_DEFLECTION_MAX)) {
		LOG_ERR("Self-test failed: X:%6d Y:%6d Z:%6d", diff_mg.x, diff_mg.y, diff_mg.z);
		return -EINVAL;
	}
	LOG_DBG("Difference = X:%6d Y:%6d Z:%6d", diff_mg.x, diff_mg.y, diff_mg.z);

	/* Soft reset back to low power state */
	return lis2dw12_low_power_reset(dev);
}

#endif /* CONFIG_INFUSE_IMU_SELF_TEST */

static int lis2dw12_pm_control(const struct device *dev, enum pm_device_action action)
{
	const struct lis2dw12_config *config = dev->config;
	uint8_t chip_id;
	int rc = 0;

	switch (action) {
	case PM_DEVICE_ACTION_SUSPEND:
	case PM_DEVICE_ACTION_RESUME:
	case PM_DEVICE_ACTION_TURN_OFF:
		break;
	case PM_DEVICE_ACTION_TURN_ON:
		/* Configure GPIO */
		gpio_pin_configure_dt(&config->irq_gpio, GPIO_INPUT);
		/* Registers accessible after this delay */
		k_sleep(K_MSEC(10));
		/* Initialise the bus */
		rc = lis2dw12_bus_init(dev);
		if (rc < 0) {
			LOG_ERR("Cannot communicate with IMU");
			return rc;
		}
		/* Check communications with the device */
		rc = lis2dw12_reg_read(dev, LIS2DW12_REG_WHO_AM_I, &chip_id, 1);
		if ((rc < 0) || (chip_id != LIS2DW12_WHO_AM_I)) {
			LOG_ERR("Invalid chip ID %02X", chip_id);
			return -EIO;
		}
		rc = lis2dw12_trim_reset(dev);
		if (rc < 0) {
			LOG_DBG("Trim reset did not complete");
			return -EIO;
		}
		/* Soft reset back to low power state */
		rc = lis2dw12_low_power_reset(dev);
		break;
	default:
		return -ENOTSUP;
	}

	return rc;
}

static int lis2dw12_init(const struct device *dev)
{
	const struct lis2dw12_config *config = dev->config;
	struct lis2dw12_data *data = dev->data;

	/* Initialise data structures */
	gpio_init_callback(&data->int_cb, lis2dw12_gpio_callback, BIT(config->irq_gpio.pin));
	/* Enable the INT1 GPIO */
	if (gpio_add_callback(config->irq_gpio.port, &data->int_cb) < 0) {
		LOG_ERR("Could not set gpio callback");
		return -EIO;
	}
	k_sem_init(&data->int_sem, 0, 1);

	if (lis2dw12_bus_check(dev) < 0) {
		LOG_DBG("Bus not ready");
		return -EIO;
	}

	return pm_device_driver_init(dev, lis2dw12_pm_control);
}

struct infuse_imu_api lis2dw12_imu_api = {
	.configure = lis2dw12_configure,
	.data_wait = lis2dw12_data_wait,
	.data_read = lis2dw12_data_read,
#ifdef CONFIG_INFUSE_IMU_SELF_TEST
	.self_test = lis2dw12_self_test,
#endif
};

/* Initializes a struct lis2dw12_config for an instance on a SPI bus. */
#define LIS2DW12_CONFIG_SPI(inst)                                                                  \
	.bus.spi = SPI_DT_SPEC_INST_GET(inst, SPI_WORD_SET(8) | SPI_TRANSFER_MSB),                 \
	.bus_io = &lis2dw12_bus_io_spi,

/* Initializes a struct lis2dw12_config for an instance on an I2C bus. */
#define LIS2DW12_CONFIG_I2C(inst)                                                                  \
	.bus.i2c = I2C_DT_SPEC_INST_GET(inst), .bus_io = &lis2dw12_bus_io_i2c,

#define LIS2DW12_CTRL6_BASE(inst)                                                                  \
	((DT_INST_PROP(inst, bw_filt) << 6) |                                                      \
	 COND_CODE_1(DT_INST_PROP(inst, low_noise), (LIS2DW_CTRL6_LOW_NOISE), (0)))

#define LIS2DW12_INST(inst)                                                                        \
	BUILD_ASSERT(DT_INST_PROP(inst, power_mode) != 4);                                         \
	static struct lis2dw12_data lis2dw12_drv_##inst;                                           \
	static const struct lis2dw12_config lis2dw12_config_##inst = {                             \
		.irq_gpio = GPIO_DT_SPEC_INST_GET_BY_IDX(inst, irq_gpios, 0),                      \
		.lp_mode = DT_INST_PROP(inst, power_mode),                                         \
		.ctrl6_base = LIS2DW12_CTRL6_BASE(inst),                                           \
		COND_CODE_1(DT_INST_ON_BUS(inst, spi), (LIS2DW12_CONFIG_SPI(inst)),                \
			    (LIS2DW12_CONFIG_I2C(inst)))};                                         \
	PM_DEVICE_DT_INST_DEFINE(inst, lis2dw12_pm_control);                                       \
	DEVICE_DT_INST_DEFINE(inst, lis2dw12_init, PM_DEVICE_DT_INST_GET(inst),                    \
			      &lis2dw12_drv_##inst, &lis2dw12_config_##inst, POST_KERNEL,          \
			      CONFIG_SENSOR_INIT_PRIORITY, &lis2dw12_imu_api);

DT_INST_FOREACH_STATUS_OKAY(LIS2DW12_INST);
