/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#define DT_DRV_COMPAT embeint_imu_emul

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/random/random.h>

#include <infuse/drivers/imu.h>

struct emul_config {
};

struct emul_data {
	struct k_timer data_timer;
	struct k_sem new_data;
	k_ticks_t timer_expiry;
	uint8_t full_scale_range;
	uint32_t sample_period_us;
	uint16_t num_samples;
};

static void timer_fired(struct k_timer *timer)
{
	struct emul_data *data = CONTAINER_OF(timer, struct emul_data, data_timer);

	data->timer_expiry = k_uptime_ticks();
	k_sem_give(&data->new_data);
}

int emul_configure(const struct device *dev, const struct imu_config *imu_cfg,
		   struct imu_config_output *output)
{
	struct emul_data *data = dev->data;
	k_timeout_t period;

	k_timer_stop(&data->data_timer);

	if ((imu_cfg == NULL) || (imu_cfg->accelerometer.sample_rate_hz == 0)) {
		return 0;
	}

	switch (imu_cfg->accelerometer.full_scale_range) {
	case 2:
	case 4:
	case 8:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	data->sample_period_us = USEC_PER_SEC / imu_cfg->accelerometer.sample_rate_hz;
	data->full_scale_range = imu_cfg->accelerometer.full_scale_range;
	data->num_samples = imu_cfg->fifo_sample_buffer;

	output->accelerometer_period_us = data->sample_period_us;
	output->gyroscope_period_us = 0;
	output->magnetometer_period_us = 0;
	output->expected_interrupt_period_us = data->sample_period_us * imu_cfg->fifo_sample_buffer;

	period = K_USEC(output->expected_interrupt_period_us);
	k_timer_start(&data->data_timer, period, period);
	return 0;
}

int emul_data_wait(const struct device *dev, k_timeout_t timeout)
{
	struct emul_data *data = dev->data;

	return k_sem_take(&data->new_data, timeout);
}

int emul_data_read(const struct device *dev, struct imu_sample_array *samples, uint16_t max_samples)
{
	struct emul_data *data = dev->data;
	int16_t one_g = imu_accelerometer_1g(data->full_scale_range);
	uint16_t num = MIN(data->num_samples, max_samples);
	uint32_t buffer_period = k_us_to_ticks_near32((num - 1) * data->sample_period_us);

	samples->accelerometer.timestamp_ticks = data->timer_expiry - buffer_period;
	samples->accelerometer.num = num;
	samples->accelerometer.offset = 0;
	samples->accelerometer.full_scale_range = data->full_scale_range;
	samples->accelerometer.buffer_period_ticks = buffer_period;

	for (int i = 0; i < num; i++) {
		samples->samples[i].x = 0;
		samples->samples[i].y = 0;
		samples->samples[i].z = one_g;
	}
	return 0;
}

static int emul_pm_control(const struct device *dev, enum pm_device_action action)
{
	return 0;
}

static int emul_init(const struct device *dev)
{
	struct emul_data *data = dev->data;

	k_sem_init(&data->new_data, 0, 1);
	k_timer_init(&data->data_timer, timer_fired, NULL);

	return pm_device_driver_init(dev, emul_pm_control);
}

struct infuse_imu_api emul_imu_api = {
	.configure = emul_configure,
	.data_wait = emul_data_wait,
	.data_read = emul_data_read,
};

#define EMUL_INST(inst)                                                                            \
	static struct emul_data emul_drv_##inst;                                                   \
	static const struct emul_config emul_config_##inst = {};                                   \
	PM_DEVICE_DT_INST_DEFINE(inst, emul_pm_control);                                           \
	DEVICE_DT_INST_DEFINE(inst, emul_init, PM_DEVICE_DT_INST_GET(inst), &emul_drv_##inst,      \
			      &emul_config_##inst, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,       \
			      &emul_imu_api);

DT_INST_FOREACH_STATUS_OKAY(EMUL_INST);
