/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#define DT_DRV_COMPAT bosch_bma4xx

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/drivers/imu.h>

#include "bma4xx.h"

#define FRAME_SIZE 7
#define FIFO_BYTES MIN(BMA4XX_FIFO_LEN, (FRAME_SIZE * CONFIG_INFUSE_IMU_MAX_FIFO_SAMPLES))

struct bma4xx_config {
	union bma4xx_bus bus;
	const struct bma4xx_bus_io *bus_io;
	struct gpio_dt_spec int1_gpio;
};

struct bma4xx_data {
	struct gpio_callback int1_cb;
	struct k_sem int1_sem;
	int64_t int1_timestamp;
	int64_t int1_prev_timestamp;
	uint8_t accel_range;
	uint16_t fifo_threshold;
	uint8_t fifo_data_buffer[FIFO_BYTES];
};

struct fifo_frame_data {
	int16_t x;
	int16_t y;
	int16_t z;
} __packed;

struct sensor_config {
	uint32_t period_us;
	uint8_t config0;
	uint8_t config1;
};

LOG_MODULE_REGISTER(bma4xx, CONFIG_SENSOR_LOG_LEVEL);

static inline int bma4xx_bus_check(const struct device *dev)
{
	const struct bma4xx_config *cfg = dev->config;

	return cfg->bus_io->check(&cfg->bus);
}

static inline int bma4xx_bus_init(const struct device *dev)
{
	const struct bma4xx_config *cfg = dev->config;

	return cfg->bus_io->init(&cfg->bus);
}

static inline int bma4xx_bus_pm(const struct device *dev, bool power_up)
{
	const struct bma4xx_config *cfg = dev->config;

	return cfg->bus_io->pm(&cfg->bus, power_up);
}

static inline int bma4xx_reg_read(const struct device *dev, uint8_t reg, void *data,
				  uint16_t length)
{
	const struct bma4xx_config *cfg = dev->config;

	return cfg->bus_io->read(&cfg->bus, reg, data, length);
}

static inline int bma4xx_reg_write(const struct device *dev, uint8_t reg, uint8_t data)
{
	const struct bma4xx_config *cfg = dev->config;

	return cfg->bus_io->write(&cfg->bus, reg, data);
}

static int bma4xx_device_init(const struct device *dev)
{
	int rc;

	/* Soft-reset the device */
	rc = bma4xx_reg_write(dev, BMA4XX_REG_CMD, BMA4XX_CMD_SOFT_RESET);
	if (rc < 0) {
		goto init_end;
	}
	k_sleep(K_USEC(BMA4XX_POR_DELAY));

	/* Re-initialise the bus */
	rc = bma4xx_bus_init(dev);
init_end:
	if (rc) {
		LOG_DBG("Cmd failed (%d)", rc);
	}
	return rc;
}

static void bma4xx_gpio_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	struct bma4xx_data *data = CONTAINER_OF(cb, struct bma4xx_data, int1_cb);

	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	LOG_DBG("");

	data->int1_prev_timestamp = data->int1_timestamp;
	data->int1_timestamp = k_uptime_ticks();
	k_sem_give(&data->int1_sem);
}

static int bma4xx_low_power_reset(const struct device *dev)
{
	const struct bma4xx_config *config = dev->config;
	struct bma4xx_data *data = dev->data;

	(void)gpio_pin_interrupt_configure_dt(&config->int1_gpio, GPIO_INT_DISABLE);
	(void)gpio_pin_configure_dt(&config->int1_gpio, GPIO_DISCONNECTED);
	(void)k_sem_take(&data->int1_sem, K_NO_WAIT);

	return bma4xx_reg_write(dev, BMA4XX_REG_CMD, BMA4XX_CMD_SOFT_RESET);
}

static struct sensor_config accel_conf(uint16_t sample_rate, uint8_t range, bool low_power,
				       uint8_t *fs_range)
{
	struct sensor_config ret;

	/* Sensing range */
	*fs_range = range;
	switch (range) {
	case 2:
		ret.config1 = BMA4XX_ACC_CONFIG1_RANGE_2G;
		break;
	case 4:
		ret.config1 = BMA4XX_ACC_CONFIG1_RANGE_4G;
		break;
	case 8:
		ret.config1 = BMA4XX_ACC_CONFIG1_RANGE_8G;
		break;
	case 16:
		ret.config1 = BMA4XX_ACC_CONFIG1_RANGE_16G;
		break;
	default:
		LOG_WRN("Default range 4G");
		*fs_range = 4;
		ret.config1 = BMA4XX_ACC_CONFIG1_RANGE_4G;
	}

	/* Sample rate selection */
	if (sample_rate < 18) {
		ret.period_us = 2 * USEC_PER_SEC / 25;
		ret.config1 |= BMA4XX_ACC_CONFIG1_ODR_25D2;
	} else if (sample_rate < 34) {
		ret.period_us = USEC_PER_SEC / 25;
		ret.config1 |= BMA4XX_ACC_CONFIG1_ODR_25;
	} else if (sample_rate < 75) {
		ret.period_us = USEC_PER_SEC / 50;
		ret.config1 |= BMA4XX_ACC_CONFIG1_ODR_50;
	} else if (sample_rate < 150) {
		ret.period_us = USEC_PER_SEC / 100;
		ret.config1 |= BMA4XX_ACC_CONFIG1_ODR_100;
	} else if (sample_rate < 300) {
		ret.period_us = USEC_PER_SEC / 200;
		ret.config1 |= BMA4XX_ACC_CONFIG1_ODR_200;
	} else if (sample_rate < 600) {
		ret.period_us = USEC_PER_SEC / 400;
		ret.config1 |= BMA4XX_ACC_CONFIG1_ODR_400;
	} else {
		ret.period_us = USEC_PER_SEC / 800;
		ret.config1 |= BMA4XX_ACC_CONFIG1_ODR_800;
	}

	/* Power configuration */
	ret.config0 = low_power ? BMA4XX_ACC_CONFIG0_POWER_MODE_LOW_POWER
				: BMA4XX_ACC_CONFIG0_POWER_MODE_NORMAL;

	return ret;
}

int bma4xx_configure(const struct device *dev, const struct imu_config *imu_cfg,
		     struct imu_config_output *output)
{
	const struct bma4xx_config *config = dev->config;
	struct bma4xx_data *data = dev->data;
	struct sensor_config config_regs;
	uint32_t frame_period_us;
	uint16_t fifo_watermark;
	int rc;

	/* Power up comms bus */
	rc = bma4xx_bus_pm(dev, true);
	if (rc < 0) {
		return rc;
	}

	/* Reset back to default state */
	rc = bma4xx_low_power_reset(dev);
	if (rc < 0) {
		goto cleanup;
	}
	/* No more work to do */
	if (imu_cfg == NULL) {
		goto cleanup;
	}
	if (imu_cfg->accelerometer.sample_rate_hz == 0) {
		if (imu_cfg->gyroscope.sample_rate_hz || imu_cfg->magnetometer.sample_rate_hz) {
			/* Only gyro or mag requested */
			rc = -ENOTSUP;
		}
		goto cleanup;
	}
	if (imu_cfg->fifo_sample_buffer == 0) {
		rc = -EINVAL;
		goto cleanup;
	}
	output->gyroscope_period_us = 0;
	output->magnetometer_period_us = 0;

	config_regs = accel_conf(imu_cfg->accelerometer.sample_rate_hz,
				 imu_cfg->accelerometer.full_scale_range,
				 imu_cfg->accelerometer.low_power, &data->accel_range);
	output->accelerometer_period_us = config_regs.period_us;
	frame_period_us = config_regs.period_us;

	/* Accelerometer configuration */
	rc |= bma4xx_reg_write(dev, BMA4XX_REG_ACC_CONFIG0, config_regs.config0);
	k_sleep(K_USEC(1500));
	rc |= bma4xx_reg_write(dev, BMA4XX_REG_ACC_CONFIG1, config_regs.config1);

	/* Interrupt configuration */
	rc |= bma4xx_reg_write(dev, BMA4XX_REG_INT_CONFIG0, BMA4XX_INT_CONFIG0_FIFO_WATERMARK);
	rc |= bma4xx_reg_write(dev, BMA4XX_REG_INT1_MAP, BMA4XX_INT_MAP_FIFO_WATERMARK);
	rc |= bma4xx_reg_write(dev, BMA4XX_REG_INT12_IO_CTRL,
			       BMA4XX_INT_IO_CTRL_INT1_ACTIVE_HIGH |
				       BMA4XX_INT_IO_CTRL_INT1_PUSH_PULL);

	/* FIFO configuration */
	rc |= bma4xx_reg_write(dev, BMA4XX_REG_FIFO_CONFIG0,
			       BMA4XX_FIFO_CONFIG0_EN_XYZ | BMA4XX_FIFO_CONFIG0_DATA_12BIT);
	fifo_watermark = MIN(FRAME_SIZE * imu_cfg->fifo_sample_buffer, FIFO_BYTES);
	rc |= bma4xx_reg_write(dev, BMA4XX_REG_FIFO_CONFIG1, (fifo_watermark >> 0) & 0xFF);
	rc |= bma4xx_reg_write(dev, BMA4XX_REG_FIFO_CONFIG2, (fifo_watermark >> 8) & 0xFF);
	data->fifo_threshold = fifo_watermark;
	LOG_DBG("Watermark: %d bytes", fifo_watermark);

	output->expected_interrupt_period_us =
		(fifo_watermark / FRAME_SIZE) * output->accelerometer_period_us;

	/* Flush the FIFO, enable GPIO interrupts */
	rc |= bma4xx_reg_write(dev, BMA4XX_REG_CMD, BMA4XX_CMD_FIFO_FLUSH);

	/* Approximate start time of data collection */
	data->int1_timestamp = k_uptime_ticks();

	/* Enable the INT1 GPIO */
	(void)gpio_pin_configure_dt(&config->int1_gpio, GPIO_INPUT);
	(void)gpio_pin_interrupt_configure_dt(&config->int1_gpio, GPIO_INT_EDGE_TO_ACTIVE);
	if (rc) {
		rc = -EIO;
	}

cleanup:
	(void)bma4xx_bus_pm(dev, false);
	return rc;
}

int bma4xx_data_wait(const struct device *dev, k_timeout_t timeout)
{
	struct bma4xx_data *data = dev->data;

	return k_sem_take(&data->int1_sem, timeout);
}

/* Data is packed as (8 MSB's, 4 0 bits, 4 LSB's).
 * We therefore need to shift up the 4 LSB's to get the real value.
 */
#define AXIS_DECODE(val) ((val) & 0xFF00) | (((val) & 0x000F) << 4)

int bma4xx_data_read(const struct device *dev, struct imu_sample_array *samples,
		     uint16_t max_samples)
{
	struct bma4xx_data *data = dev->data;
	uint16_t fifo_length, buffer_offset = 0;
	int64_t first_frame_time, last_frame_time;
	int32_t int_period_ticks;
	int32_t frame_period_ticks;
	uint16_t extra_frames, interrupt_frame = 0;
	uint8_t fh_mode, fh_param;
	uint8_t acc_samples = 0;
	bool extra_pending = false;
	int64_t sim_timestamp;
	int rc;

	/* Init sample output */
	samples->accelerometer = (struct imu_sensor_meta){0};
	samples->gyroscope = (struct imu_sensor_meta){0};
	samples->magnetometer = (struct imu_sensor_meta){0};

	/* Power up comms bus */
	rc = bma4xx_bus_pm(dev, true);
	if (rc < 0) {
		return rc;
	}

	/* Get FIFO data length */
	rc = bma4xx_reg_read(dev, BMA4XX_REG_FIFO_LENGTH0, &fifo_length, 2);
	if (rc < 0) {
		goto cleanup;
	}
	LOG_DBG("Reading %d bytes", fifo_length);

	/* More data pending than we have FIFO */
	if (fifo_length > sizeof(data->fifo_data_buffer)) {
		/* Round down to what we can actually fit in the buffer */
		fifo_length = ROUND_DOWN(sizeof(data->fifo_data_buffer), FRAME_SIZE);
		extra_pending = true;
	}

	/* Read the FIFO data */
	rc = bma4xx_reg_read(dev, BMA4XX_REG_FIFO_DATA, data->fifo_data_buffer, fifo_length);
	if (rc < 0) {
		goto cleanup;
	}

	if (extra_pending) {
		/* Reset the FIFO, since handling any remaining data is questionable */
		LOG_WRN("Flushing FIFO due to overrun");
		rc = bma4xx_reg_write(dev, BMA4XX_REG_CMD, BMA4XX_CMD_FIFO_FLUSH);
		if (rc < 0) {
			LOG_WRN("FIFO flush failed");
		}
		(void)k_sem_take(&data->int1_sem, K_NO_WAIT);
		sim_timestamp = k_uptime_ticks();
	}

	/* Scan through to populate data and count frames */
	while (buffer_offset < fifo_length) {
		/* Extract FIFO frame header params */
		fh_mode = data->fifo_data_buffer[buffer_offset] & FIFO_HEADER_MODE_MASK;
		fh_param = data->fifo_data_buffer[buffer_offset] & FIFO_HEADER_PARAM_MASK;
		buffer_offset += 1;

		if (fh_mode == FIFO_HEADER_MODE_CONTROL) {
			buffer_offset += 1;
			continue;
		}

		if (buffer_offset >= data->fifo_threshold) {
			if (interrupt_frame == 0) {
				interrupt_frame = acc_samples;
			}
		}

		if (acc_samples < max_samples) {
			struct fifo_frame_data *frame =
				(void *)(data->fifo_data_buffer + buffer_offset);

			/* Convert from fifo frame to 16bit data format */
			samples->samples[acc_samples].x = AXIS_DECODE(frame->x);
			samples->samples[acc_samples].y = AXIS_DECODE(frame->y);
			samples->samples[acc_samples].z = AXIS_DECODE(frame->z);
		}
		acc_samples++;
		buffer_offset += 6;
	}
	if (acc_samples == 0) {
		rc = -ENODATA;
		goto cleanup;
	}
	if (interrupt_frame == 0) {
		interrupt_frame = acc_samples;
	}
	extra_frames = acc_samples - interrupt_frame;

	/* Validate there is enough space for samples */
	if (acc_samples > max_samples) {
		LOG_WRN("%d > %d", acc_samples, max_samples);
		rc = -ENOMEM;
		goto cleanup;
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

	LOG_DBG("%d data frames (%d extra) at %d ticks/frame (%d us)", acc_samples, extra_frames,
		frame_period_ticks, k_ticks_to_us_near32(frame_period_ticks));

	/* Store output parameters */
	samples->accelerometer.num = acc_samples;
	samples->accelerometer.full_scale_range = data->accel_range;
	samples->accelerometer.timestamp_ticks = first_frame_time;
	samples->accelerometer.buffer_period_ticks =
		(acc_samples - 1) * int_period_ticks / interrupt_frame;

	if (extra_pending) {
		/* Set the interrupt time to the FIFO flush */
		data->int1_timestamp = sim_timestamp;
	}

cleanup:
	(void)bma4xx_bus_pm(dev, false);
	return rc;
}

#ifdef CONFIG_INFUSE_IMU_SELF_TEST

static int16_t reg_convert(uint16_t reg)
{
	int16_t sign_extended = reg & BIT(11) ? (0xF000 | reg) : reg;
	/* Shift to 16bit resolution */
	return sign_extended << 4;
}

/* Recommended self-test procedure from datasheet */
static int bma4xx_self_test(const struct device *dev)
{
	uint16_t raw_positive[3], raw_negative[3];
	int16_t acc_positive[3], acc_negative[3];
	int16_t mg_positive[3], mg_negative[3];
	int16_t mg_difference[3];
	int16_t one_g;
	int rc;

	LOG_DBG("Starting self-test procedure");

	/* Power up comms bus */
	rc = bma4xx_bus_pm(dev, true);
	if (rc < 0) {
		return rc;
	}

	/* Reset back to default state */
	rc = bma4xx_low_power_reset(dev);
	if (rc < 0) {
		goto cleanup;
	}

	/* Accelerometer enabled, OSR=3, Normal Mode */
	rc = bma4xx_reg_write(dev, BMA4XX_REG_ACC_CONFIG0, BMA4XX_ACC_CONFIG0_POWER_MODE_NORMAL);
	if (rc < 0) {
		goto cleanup;
	}
	k_sleep(K_USEC(1500));
	rc = bma4xx_reg_write(dev, BMA4XX_REG_ACC_CONFIG1,
			      BMA4XX_ACC_CONFIG1_RANGE_4G | BMA4XX_ACC_CONFIG1_ODR_100);
	if (rc < 0) {
		goto cleanup;
	}

	/* Wait for > 2ms */
	k_sleep(K_MSEC(4));

	/* Enable self-test for all axes, positive excitation */
	rc = bma4xx_reg_write(dev, BMA4XX_REG_SELF_TEST,
			      BMA4XX_SELF_TEST_POSITIVE | BMA4XX_SELF_TEST_EN_XYZ);
	if (rc < 0) {
		goto cleanup;
	}

	/* Wait for > 50ms */
	k_sleep(K_MSEC(100));

	/* Read all axis data */
	rc = bma4xx_reg_read(dev, BMA4XX_REG_ACC_X_LSB, raw_positive, sizeof(raw_positive));
	if (rc < 0) {
		goto cleanup;
	}

	/* Swap to negative excitation */
	rc = bma4xx_reg_write(dev, BMA4XX_REG_SELF_TEST,
			      BMA4XX_SELF_TEST_NEGATIVE | BMA4XX_SELF_TEST_EN_XYZ);
	if (rc < 0) {
		goto cleanup;
	}

	/* Wait for > 50ms */
	k_sleep(K_MSEC(100));

	/* Read all axis data */
	rc = bma4xx_reg_read(dev, BMA4XX_REG_ACC_X_LSB, raw_negative, sizeof(raw_negative));
	if (rc < 0) {
		goto cleanup;
	}

	/* Self-reset back to known state */
	rc = bma4xx_low_power_reset(dev);
	if (rc < 0) {
		goto cleanup;
	}

	/* Convert raw register readings to milli-g */
	one_g = imu_accelerometer_1g(4);
	for (int i = 0; i < 3; i++) {
		acc_positive[i] = reg_convert(raw_positive[i]);
		acc_negative[i] = reg_convert(raw_negative[i]);

		mg_positive[i] = (1000 * (int32_t)acc_positive[i]) / one_g;
		mg_negative[i] = (1000 * (int32_t)acc_negative[i]) / one_g;

		mg_difference[i] = mg_positive[i] - mg_negative[i];
	}

	/* Compare measured differences against specified minimums */
	if ((mg_difference[0] < BMA4XX_SELF_TEST_MINIMUM_X) ||
	    (mg_difference[1] < BMA4XX_SELF_TEST_MINIMUM_Y) ||
	    (mg_difference[2] < BMA4XX_SELF_TEST_MINIMUM_Z)) {
		LOG_ERR("Self-test failed: X:%6d Y:%6d Z:%6d", mg_difference[0], mg_difference[1],
			mg_difference[2]);
		rc = -EINVAL;
		goto cleanup;
	}
	LOG_DBG("Difference = X:%6d Y:%6d Z:%6d", mg_difference[0], mg_difference[1],
		mg_difference[2]);

cleanup:
	(void)bma4xx_bus_pm(dev, false);
	return rc;
}
#endif /* CONFIG_INFUSE_IMU_SELF_TEST */

static int bma4xx_pm_control(const struct device *dev, enum pm_device_action action)
{
	const struct bma4xx_config *config = dev->config;
	bool bus_power = false;
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

		/* Chip ready after this duration */
		k_sleep(K_USEC(BMA4XX_POR_DELAY));

		/* Power up the bus */
		rc = bma4xx_bus_pm(dev, true);
		if (rc < 0) {
			LOG_DBG("Cannot power up bus");
			break;
		}
		bus_power = true;

		/* Initialise the bus */
		rc = bma4xx_bus_init(dev);
		if (rc < 0) {
			LOG_DBG("Cannot communicate with IMU");
			break;
		}

		/* Check communications with the device */
		rc = bma4xx_reg_read(dev, BMA4XX_REG_CHIP_ID, &chip_id, 1);
		if ((rc < 0) || (chip_id != BMA4XX_CHIP_ID)) {
			LOG_ERR("Invalid chip ID %02X", chip_id);
			rc = -EIO;
			break;
		}

		/* Perform init sequence */
		rc = bma4xx_device_init(dev);
		break;
	default:
		return -ENOTSUP;
	}

	if (bus_power) {
		/* Power down the bus */
		(void)bma4xx_bus_pm(dev, false);
	}

	return rc;
}

static int bma4xx_init(const struct device *dev)
{
	const struct bma4xx_config *config = dev->config;
	struct bma4xx_data *data = dev->data;

	/* Initialise data structures */
	gpio_init_callback(&data->int1_cb, bma4xx_gpio_callback, BIT(config->int1_gpio.pin));
	if (gpio_add_callback(config->int1_gpio.port, &data->int1_cb) < 0) {
		LOG_DBG("Could not set gpio callback");
		return -EIO;
	}
	k_sem_init(&data->int1_sem, 0, 1);

	if (bma4xx_bus_check(dev) < 0) {
		LOG_DBG("Bus not ready");
		return -EIO;
	}

	return pm_device_driver_init(dev, bma4xx_pm_control);
}

struct infuse_imu_api bma4xx_imu_api = {
	.configure = bma4xx_configure,
	.data_wait = bma4xx_data_wait,
	.data_read = bma4xx_data_read,
#ifdef CONFIG_INFUSE_IMU_SELF_TEST
	.self_test = bma4xx_self_test,
#endif
};

/* Initializes a struct bma4xx_config for an instance on a SPI bus. */
#define BMA4XX_CONFIG_SPI(inst)                                                                    \
	.bus.spi = SPI_DT_SPEC_INST_GET(inst, SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0),              \
	.bus_io = &bma4xx_bus_io_spi,

/* Initializes a struct bma4xx_config for an instance on an I2C bus. */
#define BMA4XX_CONFIG_I2C(inst) .bus.i2c = I2C_DT_SPEC_INST_GET(inst), .bus_io = &bma4xx_bus_io_i2c,

#define BMA4XX_INST(inst)                                                                          \
	static struct bma4xx_data bma4xx_drv_##inst;                                               \
	static const struct bma4xx_config bma4xx_config_##inst = {                                 \
		.int1_gpio = GPIO_DT_SPEC_INST_GET_BY_IDX(inst, int1_gpios, 0),                    \
		COND_CODE_1(DT_INST_ON_BUS(inst, spi), (BMA4XX_CONFIG_SPI(inst)),                  \
			    (BMA4XX_CONFIG_I2C(inst)))};                                           \
	PM_DEVICE_DT_INST_DEFINE(inst, bma4xx_pm_control);                                         \
	DEVICE_DT_INST_DEFINE(inst, bma4xx_init, PM_DEVICE_DT_INST_GET(inst), &bma4xx_drv_##inst,  \
			      &bma4xx_config_##inst, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,     \
			      &bma4xx_imu_api);

DT_INST_FOREACH_STATUS_OKAY(BMA4XX_INST);
