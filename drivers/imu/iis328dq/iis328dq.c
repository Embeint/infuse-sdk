/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 * The IIS328DQ has no hardware FIFO, so samples are buffered in software. Each data ready
 * interrupt wakes a driver thread which reads the sample into a buffer. The "FIFO interrupt"
 * exposed through @ref imu_data_wait is generated once the requested number of samples has
 * been buffered.
 */

#define DT_DRV_COMPAT st_iis328dq

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <infuse/drivers/imu.h>

#include "iis328dq.h"

struct iis328dq_config {
	union iis328dq_bus bus;
	const struct iis328dq_bus_io *bus_io;
	struct gpio_dt_spec drdy_gpio;
	k_thread_stack_t *stack;
	size_t stack_size;
	uint8_t ctrl3_drdy;
};

struct iis328dq_data {
	const struct device *dev;
	struct gpio_callback int_cb;
	struct k_thread thread;
	/* Data ready interrupt -> driver thread */
	struct k_sem drdy_sem;
	/* Sample threshold reached -> imu_data_wait */
	struct k_sem int_sem;
	struct k_spinlock lock;
	int64_t drdy_timestamp;
	/* Software FIFO state (protected by lock) */
	int64_t first_timestamp;
	int64_t last_timestamp;
	uint16_t num_samples;
	uint16_t threshold;
	bool threshold_signalled;
	bool overrun;
	bool running;
	uint8_t accel_range;
	struct imu_sample samples[CONFIG_INFUSE_IMU_MAX_FIFO_SAMPLES];
};

struct sensor_config {
	uint32_t period_us;
	uint8_t ctrl1;
	uint8_t ctrl4;
};

LOG_MODULE_REGISTER(iis328dq, CONFIG_SENSOR_LOG_LEVEL);

static inline int iis328dq_reg_read(const struct device *dev, uint8_t reg, void *data,
				    uint16_t length)
{
	const struct iis328dq_config *cfg = dev->config;

	return cfg->bus_io->read(&cfg->bus, reg, data, length);
}

static inline int iis328dq_reg_write(const struct device *dev, uint8_t reg, uint8_t value)
{
	const struct iis328dq_config *cfg = dev->config;

	return cfg->bus_io->write(&cfg->bus, reg, &value, 1);
}

static int iis328dq_low_power_reset(const struct device *dev)
{
	const struct iis328dq_config *config = dev->config;
	struct iis328dq_data *data = dev->data;
	k_spinlock_key_t key;
	int rc;

	/* Disable the IRQ GPIO and stop buffering samples */
	(void)gpio_pin_interrupt_configure_dt(&config->drdy_gpio, GPIO_INT_DISABLE);
	key = k_spin_lock(&data->lock);
	data->running = false;
	k_spin_unlock(&data->lock, key);

	/* There is no soft-reset, restore the default register values */
	rc = iis328dq_reg_write(dev, IIS328DQ_REG_CTRL1, IIS328DQ_CTRL1_PM_POWER_DOWN);
	rc |= iis328dq_reg_write(dev, IIS328DQ_REG_CTRL2, 0x00);
	rc |= iis328dq_reg_write(dev, IIS328DQ_REG_CTRL3, 0x00);
	rc |= iis328dq_reg_write(dev, IIS328DQ_REG_CTRL4, IIS328DQ_CTRL4_BDU);
	rc |= iis328dq_reg_write(dev, IIS328DQ_REG_CTRL5, 0x00);
	return rc ? -EIO : 0;
}

static void iis328dq_gpio_callback(const struct device *port, struct gpio_callback *cb,
				   uint32_t pins)
{
	struct iis328dq_data *data = CONTAINER_OF(cb, struct iis328dq_data, int_cb);

	ARG_UNUSED(port);
	ARG_UNUSED(pins);

	data->drdy_timestamp = k_uptime_ticks();
	k_sem_give(&data->drdy_sem);
}

static void iis328dq_sample_push(struct iis328dq_data *data,
				 const struct iis328dq_status_frame *frame, int64_t timestamp)
{
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	if (!data->running) {
		goto end;
	}
	if (frame->status & IIS328DQ_STATUS_ZYXOR) {
		/* Sample was overwritten before it could be read */
		data->overrun = true;
	}
	if (data->num_samples >= ARRAY_SIZE(data->samples)) {
		/* Software FIFO is full, drop the newest sample */
		data->overrun = true;
		goto end;
	}
	if (data->num_samples == 0) {
		data->first_timestamp = timestamp;
	}
	data->last_timestamp = timestamp;
	data->samples[data->num_samples++] = (struct imu_sample){
		.x = sys_le16_to_cpu(frame->x),
		.y = sys_le16_to_cpu(frame->y),
		.z = sys_le16_to_cpu(frame->z),
	};

	if ((data->num_samples >= data->threshold) && !data->threshold_signalled) {
		data->threshold_signalled = true;
		k_sem_give(&data->int_sem);
	}
end:
	k_spin_unlock(&data->lock, key);
}

static void iis328dq_thread(void *p1, void *p2, void *p3)
{
	const struct device *dev = p1;
	const struct iis328dq_config *config = dev->config;
	struct iis328dq_data *data = dev->data;
	struct iis328dq_status_frame frame;
	int64_t timestamp;
	int rc;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
		k_sem_take(&data->drdy_sem, K_FOREVER);
		timestamp = data->drdy_timestamp;

		do {
			/* Read the status and output registers in a single transaction */
			rc = iis328dq_reg_read(dev, IIS328DQ_REG_STATUS, &frame, sizeof(frame));
			if (rc < 0) {
				LOG_WRN("Failed to read sample (%d)", rc);
				break;
			}
			if (!(frame.status & IIS328DQ_STATUS_ZYXDA)) {
				break;
			}
			iis328dq_sample_push(data, &frame, timestamp);
			/* The data ready line only generates an edge when it is deasserted by
			 * reading the sample. If a new sample arrived during the read, the line
			 * may still be asserted and no further edge will be generated.
			 */
			timestamp = k_uptime_ticks();
		} while (data->running && (gpio_pin_get_dt(&config->drdy_gpio) == 1));
	}
}

static struct sensor_config accel_conf(uint16_t sample_rate, uint8_t range, uint8_t *fs_range)
{
	struct sensor_config ret;

	/* Sensing range */
	*fs_range = range;
	switch (range) {
	case 2:
		ret.ctrl4 = IIS328DQ_CTRL4_FS_2G;
		break;
	case 4:
		ret.ctrl4 = IIS328DQ_CTRL4_FS_4G;
		break;
	case 8:
		ret.ctrl4 = IIS328DQ_CTRL4_FS_8G;
		break;
	default:
		LOG_WRN("Default range 4G");
		*fs_range = 4;
		ret.ctrl4 = IIS328DQ_CTRL4_FS_4G;
	}
	ret.ctrl4 |= IIS328DQ_CTRL4_BDU;

	/* Sample rate selection, low-power modes for rates below the normal mode minimum */
	if (sample_rate < 2) {
		ret.period_us = USEC_PER_SEC;
		ret.ctrl1 = IIS328DQ_CTRL1_PM_LP_1HZ;
	} else if (sample_rate < 4) {
		ret.period_us = USEC_PER_SEC / 2;
		ret.ctrl1 = IIS328DQ_CTRL1_PM_LP_2HZ;
	} else if (sample_rate < 8) {
		ret.period_us = USEC_PER_SEC / 5;
		ret.ctrl1 = IIS328DQ_CTRL1_PM_LP_5HZ;
	} else if (sample_rate < 20) {
		ret.period_us = USEC_PER_SEC / 10;
		ret.ctrl1 = IIS328DQ_CTRL1_PM_LP_10HZ;
	} else if (sample_rate < 75) {
		ret.period_us = USEC_PER_SEC / 50;
		ret.ctrl1 = IIS328DQ_CTRL1_PM_NORMAL | IIS328DQ_CTRL1_DR_50HZ;
	} else if (sample_rate < 250) {
		ret.period_us = USEC_PER_SEC / 100;
		ret.ctrl1 = IIS328DQ_CTRL1_PM_NORMAL | IIS328DQ_CTRL1_DR_100HZ;
	} else if (sample_rate < 700) {
		ret.period_us = USEC_PER_SEC / 400;
		ret.ctrl1 = IIS328DQ_CTRL1_PM_NORMAL | IIS328DQ_CTRL1_DR_400HZ;
	} else {
		ret.period_us = USEC_PER_SEC / 1000;
		ret.ctrl1 = IIS328DQ_CTRL1_PM_NORMAL | IIS328DQ_CTRL1_DR_1000HZ;
	}
	ret.ctrl1 |= IIS328DQ_CTRL1_XYZ_EN;

	return ret;
}

static int iis328dq_configure(const struct device *dev, const struct imu_config *imu_cfg,
			      struct imu_config_output *output)
{
	const struct iis328dq_config *config = dev->config;
	struct iis328dq_data *data = dev->data;
	struct iis328dq_status_frame frame;
	struct sensor_config config_regs;
	k_spinlock_key_t key;
	int rc;

	/* Reset back to low power state */
	rc = iis328dq_low_power_reset(dev);
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
			return -ENOTSUP;
		}
		return 0;
	}
	if (imu_cfg->fifo_sample_buffer == 0) {
		return -EINVAL;
	}

	config_regs = accel_conf(imu_cfg->accelerometer.sample_rate_hz,
				 imu_cfg->accelerometer.full_scale_range, &data->accel_range);

	/* Reset the software FIFO */
	key = k_spin_lock(&data->lock);
	data->threshold = MIN(imu_cfg->fifo_sample_buffer, ARRAY_SIZE(data->samples));
	data->num_samples = 0;
	data->threshold_signalled = false;
	data->overrun = false;
	k_spin_unlock(&data->lock, key);
	k_sem_reset(&data->int_sem);
	k_sem_reset(&data->drdy_sem);

	output->accelerometer_period_us = config_regs.period_us;
	output->gyroscope_period_us = 0;
	output->magnetometer_period_us = 0;
	output->expected_interrupt_period_us = data->threshold * config_regs.period_us;

	/* Route data ready to the interrupt pin, matching the GPIO polarity */
	rc = iis328dq_reg_write(
		dev, IIS328DQ_REG_CTRL3,
		config->ctrl3_drdy |
			((config->drdy_gpio.dt_flags & GPIO_ACTIVE_LOW) ? IIS328DQ_CTRL3_IHL : 0));
	rc |= iis328dq_reg_write(dev, IIS328DQ_REG_CTRL4, config_regs.ctrl4);
	/* Discard any stale sample, which also deasserts the data ready line */
	rc |= iis328dq_reg_read(dev, IIS328DQ_REG_STATUS, &frame, sizeof(frame));
	if (rc) {
		return -EIO;
	}

	/* Start buffering samples */
	key = k_spin_lock(&data->lock);
	data->running = true;
	k_spin_unlock(&data->lock, key);
	(void)gpio_pin_interrupt_configure_dt(&config->drdy_gpio, GPIO_INT_EDGE_TO_ACTIVE);

	/* Enable the accelerometer */
	rc = iis328dq_reg_write(dev, IIS328DQ_REG_CTRL1, config_regs.ctrl1);
	if (rc) {
		(void)iis328dq_low_power_reset(dev);
		return -EIO;
	}
	return 0;
}

static int iis328dq_data_wait(const struct device *dev, k_timeout_t timeout)
{
	struct iis328dq_data *data = dev->data;

	return k_sem_take(&data->int_sem, timeout);
}

static int iis328dq_data_read(const struct device *dev, struct imu_sample_array *samples,
			      uint16_t max_samples)
{
	struct iis328dq_data *data = dev->data;
	k_spinlock_key_t key;
	uint16_t num;
	int rc = 0;

	/* Init sample output */
	samples->accelerometer = (struct imu_sensor_meta){0};
	samples->gyroscope = (struct imu_sensor_meta){0};
	samples->magnetometer = (struct imu_sensor_meta){0};

	key = k_spin_lock(&data->lock);
	num = data->num_samples;
	if (num > max_samples) {
		k_spin_unlock(&data->lock, key);
		LOG_WRN("%d > %d", num, max_samples);
		return -ENOMEM;
	}

	samples->accelerometer.num = num;
	samples->accelerometer.full_scale_range = data->accel_range;
	samples->accelerometer.timestamp_ticks = data->first_timestamp;
	samples->accelerometer.buffer_period_ticks = data->last_timestamp - data->first_timestamp;
	memcpy(samples->samples, data->samples, num * sizeof(struct imu_sample));
	if (data->overrun) {
		/* Samples were dropped */
		rc = 1;
	}

	/* Reset the software FIFO */
	data->num_samples = 0;
	data->threshold_signalled = false;
	data->overrun = false;
	k_sem_reset(&data->int_sem);
	k_spin_unlock(&data->lock, key);

	LOG_DBG("%d data frames over %d ticks", num, samples->accelerometer.buffer_period_ticks);
	return rc;
}

#ifdef CONFIG_INFUSE_IMU_SELF_TEST

static int run_test_sequence(const struct device *dev, int16_t average[3])
{
	struct iis328dq_status_frame frame;
	int32_t sum[3] = {0};
	int rc;

	/* Wait for output to stabilise (99% after 3/ODR) */
	k_sleep(K_MSEC(100));

	/* Average 5 samples, discarding the first */
	for (int i = 0; i < 6; i++) {
		for (int j = 0; j < 100; j++) {
			rc = iis328dq_reg_read(dev, IIS328DQ_REG_STATUS, &frame, sizeof(frame));
			if (rc < 0) {
				return rc;
			}
			if (frame.status & IIS328DQ_STATUS_ZYXDA) {
				break;
			}
			k_sleep(K_MSEC(1));
		}
		if (!(frame.status & IIS328DQ_STATUS_ZYXDA)) {
			return -ETIMEDOUT;
		}
		if (i == 0) {
			continue;
		}
		/* Convert to 12-bit representation */
		sum[0] += (int16_t)sys_le16_to_cpu(frame.x) >> 4;
		sum[1] += (int16_t)sys_le16_to_cpu(frame.y) >> 4;
		sum[2] += (int16_t)sys_le16_to_cpu(frame.z) >> 4;
	}

	for (int i = 0; i < 3; i++) {
		average[i] = sum[i] / 5;
	}
	return 0;
}

static int iis328dq_self_test(const struct device *dev)
{
	int16_t base[3], pos[3], diff[3];
	int rc;

	LOG_DBG("Starting self-test procedure");

	rc = iis328dq_low_power_reset(dev);
	if (rc < 0) {
		return rc;
	}

	/* FS = 2g (self-test limits are specified at 2g); ODR = 50Hz */
	rc = iis328dq_reg_write(dev, IIS328DQ_REG_CTRL4, IIS328DQ_CTRL4_BDU | IIS328DQ_CTRL4_FS_2G);
	rc |= iis328dq_reg_write(dev, IIS328DQ_REG_CTRL1,
				 IIS328DQ_CTRL1_PM_NORMAL | IIS328DQ_CTRL1_DR_50HZ |
					 IIS328DQ_CTRL1_XYZ_EN);
	if (rc) {
		return -EIO;
	}

	/* Run the base test case */
	rc = run_test_sequence(dev, base);
	if (rc < 0) {
		goto end;
	}

	/* Enable positive sign self-test */
	rc = iis328dq_reg_write(dev, IIS328DQ_REG_CTRL4,
				IIS328DQ_CTRL4_BDU | IIS328DQ_CTRL4_FS_2G | IIS328DQ_CTRL4_ST);
	if (rc < 0) {
		goto end;
	}

	/* Run the positive excitation case */
	rc = run_test_sequence(dev, pos);
	if (rc < 0) {
		goto end;
	}

	for (int i = 0; i < 3; i++) {
		diff[i] = pos[i] - base[i];
	}

	/* Compare measured differences against specified range */
	if (!IN_RANGE(diff[0], IIS328DQ_SELF_TEST_X_MIN, IIS328DQ_SELF_TEST_X_MAX) ||
	    !IN_RANGE(diff[1], IIS328DQ_SELF_TEST_Y_MIN, IIS328DQ_SELF_TEST_Y_MAX) ||
	    !IN_RANGE(diff[2], IIS328DQ_SELF_TEST_Z_MIN, IIS328DQ_SELF_TEST_Z_MAX)) {
		LOG_ERR("Self-test failed: X:%6d Y:%6d Z:%6d", diff[0], diff[1], diff[2]);
		rc = -EINVAL;
		goto end;
	}
	LOG_DBG("Difference = X:%6d Y:%6d Z:%6d", diff[0], diff[1], diff[2]);

end:
	/* Reset back to low power state */
	if (iis328dq_low_power_reset(dev) < 0) {
		return -EIO;
	}
	return rc < 0 ? rc : 0;
}

#endif /* CONFIG_INFUSE_IMU_SELF_TEST */

static int iis328dq_pm_control(const struct device *dev, enum pm_device_action action)
{
	const struct iis328dq_config *config = dev->config;
	uint8_t reg;
	int rc = 0;

	switch (action) {
	case PM_DEVICE_ACTION_SUSPEND:
	case PM_DEVICE_ACTION_RESUME:
	case PM_DEVICE_ACTION_TURN_OFF:
		break;
	case PM_DEVICE_ACTION_TURN_ON:
		/* Configure GPIO */
		rc = gpio_pin_configure_dt(&config->drdy_gpio, GPIO_INPUT);
		if (rc < 0) {
			return rc;
		}
		/* Registers accessible after this delay */
		k_sleep(K_MSEC(IIS328DQ_BOOT_DELAY_MS));
		/* Check communications with the device */
		rc = iis328dq_reg_read(dev, IIS328DQ_REG_WHO_AM_I, &reg, 1);
		if ((rc < 0) || (reg != IIS328DQ_WHO_AM_I)) {
			LOG_ERR("Invalid chip ID %02X", reg);
			return -EIO;
		}
		/* Reload the trimming parameters. The boot process does not complete while
		 * the device is in power-down mode, so temporarily enable normal mode.
		 */
		rc = iis328dq_reg_write(dev, IIS328DQ_REG_CTRL1, IIS328DQ_CTRL1_PM_NORMAL);
		rc |= iis328dq_reg_write(dev, IIS328DQ_REG_CTRL2, IIS328DQ_CTRL2_BOOT);
		if (rc) {
			return -EIO;
		}
		for (int i = 0; i < IIS328DQ_BOOT_RELOAD_TIMEOUT_MS; i++) {
			k_sleep(K_MSEC(1));
			rc = iis328dq_reg_read(dev, IIS328DQ_REG_CTRL2, &reg, 1);
			if (rc < 0) {
				return rc;
			}
			if (!(reg & IIS328DQ_CTRL2_BOOT)) {
				LOG_DBG("Boot completed after %d ms", i + 1);
				break;
			}
		}
		if (reg & IIS328DQ_CTRL2_BOOT) {
			LOG_DBG("Boot did not complete");
			return -EIO;
		}
		/* Reset back to low power state */
		rc = iis328dq_low_power_reset(dev);
		break;
	default:
		return -ENOTSUP;
	}

	return rc;
}

static int iis328dq_init(const struct device *dev)
{
	const struct iis328dq_config *config = dev->config;
	struct iis328dq_data *data = dev->data;

	data->dev = dev;
	k_sem_init(&data->drdy_sem, 0, 1);
	k_sem_init(&data->int_sem, 0, 1);

	if (config->bus_io->check(&config->bus) < 0) {
		LOG_DBG("Bus not ready");
		return -EIO;
	}
	if (!gpio_is_ready_dt(&config->drdy_gpio)) {
		LOG_DBG("GPIO not ready");
		return -EIO;
	}

	gpio_init_callback(&data->int_cb, iis328dq_gpio_callback, BIT(config->drdy_gpio.pin));
	if (gpio_add_callback(config->drdy_gpio.port, &data->int_cb) < 0) {
		LOG_ERR("Could not set gpio callback");
		return -EIO;
	}

	/* Thread that reads samples from the device as they become available */
	k_thread_create(&data->thread, config->stack, config->stack_size, iis328dq_thread,
			(void *)dev, NULL, NULL, CONFIG_INFUSE_IMU_IIS328DQ_THREAD_PRIORITY, 0,
			K_NO_WAIT);
	k_thread_name_set(&data->thread, dev->name);

	return pm_device_driver_init(dev, iis328dq_pm_control);
}

static const struct infuse_imu_api iis328dq_imu_api = {
	.configure = iis328dq_configure,
	.data_wait = iis328dq_data_wait,
	.data_read = iis328dq_data_read,
#ifdef CONFIG_INFUSE_IMU_SELF_TEST
	.self_test = iis328dq_self_test,
#endif
};

/* Initializes a struct iis328dq_config for an instance on a SPI bus. */
#define IIS328DQ_CONFIG_SPI(inst)                                                                  \
	.bus.spi = SPI_DT_SPEC_INST_GET(inst, SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL | \
						      SPI_MODE_CPHA),                              \
	.bus_io = &iis328dq_bus_io_spi,

/* Initializes a struct iis328dq_config for an instance on an I2C bus. */
#define IIS328DQ_CONFIG_I2C(inst)                                                                  \
	.bus.i2c = I2C_DT_SPEC_INST_GET(inst), .bus_io = &iis328dq_bus_io_i2c,

/* Data ready is routed to INT1 unless `drdy-int-pad = <2>` */
#define IIS328DQ_DRDY_INT2(inst) IS_EQ(DT_INST_PROP_OR(inst, drdy_int_pad, 1), 2)

#define IIS328DQ_INST(inst)                                                                        \
	BUILD_ASSERT(COND_CODE_1(IIS328DQ_DRDY_INT2(inst),                                         \
				 (DT_INST_NODE_HAS_PROP(inst, int2_gpios)),                        \
				 (DT_INST_NODE_HAS_PROP(inst, int1_gpios))),                       \
		     "Data ready interrupt GPIO not defined");                                     \
	K_KERNEL_STACK_DEFINE(iis328dq_stack_##inst,                                               \
			      CONFIG_INFUSE_IMU_IIS328DQ_THREAD_STACK_SIZE);                       \
	static struct iis328dq_data iis328dq_drv_##inst;                                           \
	static const struct iis328dq_config iis328dq_config_##inst = {                             \
		.drdy_gpio = COND_CODE_1(IIS328DQ_DRDY_INT2(inst),                                 \
					 (GPIO_DT_SPEC_INST_GET(inst, int2_gpios)),                \
					 (GPIO_DT_SPEC_INST_GET(inst, int1_gpios))),               \
		.ctrl3_drdy = COND_CODE_1(IIS328DQ_DRDY_INT2(inst), (IIS328DQ_CTRL3_I2_CFG_DRDY),  \
					  (IIS328DQ_CTRL3_I1_CFG_DRDY)),                           \
		.stack = iis328dq_stack_##inst,                                                    \
		.stack_size = K_KERNEL_STACK_SIZEOF(iis328dq_stack_##inst),                        \
		COND_CODE_1(DT_INST_ON_BUS(inst, spi), (IIS328DQ_CONFIG_SPI(inst)),                \
			    (IIS328DQ_CONFIG_I2C(inst)))};                                         \
	PM_DEVICE_DT_INST_DEFINE(inst, iis328dq_pm_control);                                       \
	DEVICE_DT_INST_DEFINE(inst, iis328dq_init, PM_DEVICE_DT_INST_GET(inst),                    \
			      &iis328dq_drv_##inst, &iis328dq_config_##inst, POST_KERNEL,          \
			      CONFIG_SENSOR_INIT_PRIORITY, &iis328dq_imu_api);

DT_INST_FOREACH_STATUS_OKAY(IIS328DQ_INST);
