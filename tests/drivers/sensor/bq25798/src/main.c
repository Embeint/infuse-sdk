/*
 * Copyright (c) 2026 Embeint Holdings Pty Ltd
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */
#define DT_DRV_COMPAT ti_bq25798

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "bq25798.h"

/* Test-only register/interrupt model. Production code is exercised through sensor APIs. */
struct charger_emul {
	uint8_t regs[0x49];
	uint8_t initial_regs[0x49];
	struct gpio_dt_spec irq;
	struct k_work_delayable conversion;
	struct k_work_delayable unrelated;
	int fail_read;
	int fail_write;
	int mppt_writes;
	int mppt_reads;
	int adc_reads;
	bool reject_enable;
	bool complete;
	bool completion_irq;
	bool spurious_irq;
	bool storm;
	bool converting;
	bool fail_during_conversion;
};

static void pulse_irq(struct charger_emul *data)
{
	gpio_emul_input_set(data->irq.port, data->irq.pin, 0);
	gpio_emul_input_set(data->irq.port, data->irq.pin, 1);
}

static void conversion_done(struct k_work *work)
{
	struct charger_emul *data =
		CONTAINER_OF(k_work_delayable_from_work(work), struct charger_emul, conversion);

	/* Only publish the new ADC value once conversion has completed. */
	sys_put_be16(3123, &data->regs[BQ25798_REG_VBAT_ADC]);
	data->regs[BQ25798_REG_CHARGER_FLAG_2] |= BQ25798_CHARGER_FLAG_2_ADC_DONE;
	data->converting = false;
	if (data->completion_irq) {
		pulse_irq(data);
	}
}

static void unrelated_irq(struct k_work *work)
{
	struct charger_emul *data =
		CONTAINER_OF(k_work_delayable_from_work(work), struct charger_emul, unrelated);

	pulse_irq(data);
	if (data->storm) {
		k_work_reschedule(&data->unrelated, K_MSEC(10));
	}
}

static int transfer(const struct emul *emul, struct i2c_msg *msgs, int num_msgs, int addr)
{
	struct charger_emul *data = emul->data;
	uint8_t reg = msgs[0].buf[0];

	if (num_msgs == 2) {
		if (reg == data->fail_read) {
			return -EIO;
		}
		if (reg + msgs[1].len > sizeof(data->regs)) {
			return -EINVAL;
		}
		if (reg == BQ25798_REG_MPPT_CONTROL) {
			data->mppt_reads++;
		}
		if (reg == BQ25798_REG_IBUS_ADC) {
			data->adc_reads++;
		}
		memcpy(msgs[1].buf, &data->regs[reg], msgs[1].len);
		/* FLAG2 is read-to-clear, unlike the status registers. */
		if ((reg <= BQ25798_REG_CHARGER_FLAG_2) &&
		    (reg + msgs[1].len > BQ25798_REG_CHARGER_FLAG_2)) {
			data->regs[BQ25798_REG_CHARGER_FLAG_2] = 0;
		}
		return 0;
	}
	if ((num_msgs != 1) || (reg + msgs[0].len - 1 > sizeof(data->regs))) {
		return -EINVAL;
	}
	if (reg == data->fail_write) {
		return -EIO;
	}
	memcpy(&data->regs[reg], &msgs[0].buf[1], msgs[0].len - 1);
	if (reg == BQ25798_REG_MPPT_CONTROL) {
		data->mppt_writes++;
		if (data->reject_enable ||
		    (data->regs[BQ25798_REG_CHARGER_STATUS_3] & BQ25798_CHARGER_STATUS_3_VSYS) ||
		    !(data->regs[BQ25798_REG_CHARGER_STATUS_0] &
		      BQ25798_CHARGER_STATUS_0_VBUS_PRESENT)) {
			data->regs[reg] &= ~BQ25798_MPPT_CONTROL_MPPT_ENABLE;
		}
	}
	if (reg == BQ25798_REG_ADC_CONTROL) {
		data->converting = true;
		if (data->fail_during_conversion) {
			data->fail_read = BQ25798_REG_CHARGER_FLAG_2;
		}
		if (data->complete) {
			k_work_reschedule(&data->conversion, K_MSEC(40));
		}
		if (data->spurious_irq || data->storm) {
			k_work_reschedule(&data->unrelated, K_MSEC(5));
		}
	}
	return 0;
}

static int emul_init(const struct emul *emul, const struct device *parent)
{
	struct charger_emul *data = emul->data;

	data->fail_read = -1;
	data->fail_write = -1;
	data->regs[BQ25798_REG_PART_INFO] = 0x19;
	/* Simulate initialization with a battery below VSYSMIN. */
	data->regs[BQ25798_REG_CHARGER_STATUS_3] = BQ25798_CHARGER_STATUS_3_VSYS;
	k_work_init_delayable(&data->conversion, conversion_done);
	k_work_init_delayable(&data->unrelated, unrelated_irq);
	int rc = gpio_pin_configure_dt(&data->irq, GPIO_INPUT);

	return rc ? rc : gpio_emul_input_set(data->irq.port, data->irq.pin, 1);
}

static const struct i2c_emul_api emul_api = {.transfer = transfer};

#define CHARGER_EMUL(inst)                                                                         \
	static struct charger_emul emul_data_##inst = {                                            \
		.irq = GPIO_DT_SPEC_INST_GET(inst, int_gpios),                                     \
	};                                                                                         \
	EMUL_DT_INST_DEFINE(inst, emul_init, &emul_data_##inst, NULL, &emul_api, NULL);
DT_INST_FOREACH_STATUS_OKAY(CHARGER_EMUL)

static const struct device *const charger = DEVICE_DT_GET(DT_NODELABEL(charger));
static const struct device *const disabled = DEVICE_DT_GET(DT_NODELABEL(charger_disabled));
static struct charger_emul *const model = EMUL_DT_GET(DT_NODELABEL(charger))->data;
static struct charger_emul *const disabled_model =
	EMUL_DT_GET(DT_NODELABEL(charger_disabled))->data;

static void reset_model(struct charger_emul *data)
{
	memcpy(data->regs, data->initial_regs, sizeof(data->regs));
	data->regs[BQ25798_REG_CHARGER_STATUS_0] =
		BQ25798_CHARGER_STATUS_0_VBUS_PRESENT | BQ25798_CHARGER_STATUS_0_POWER_GOOD;
	data->regs[BQ25798_REG_CHARGER_STATUS_2] = BQ25798_CHARGER_STATUS_2_VBAT_PRESENT;
	data->regs[BQ25798_REG_CHARGER_STATUS_3] = 0;
	data->regs[BQ25798_REG_CHARGER_FLAG_2] = 0;
	sys_put_be16(1234, &data->regs[BQ25798_REG_VBAT_ADC]);
	data->mppt_writes = 0;
	data->mppt_reads = 0;
	data->adc_reads = 0;
	data->fail_read = -1;
	data->fail_write = -1;
	data->reject_enable = false;
	data->complete = true;
	data->completion_irq = true;
	data->spurious_irq = false;
	data->storm = false;
	data->converting = false;
	data->fail_during_conversion = false;
}

static void *setup(void)
{
	zassert_true(device_is_ready(charger));
	zassert_true(device_is_ready(disabled));
	memcpy(model->initial_regs, model->regs, sizeof(model->regs));
	memcpy(disabled_model->initial_regs, disabled_model->regs, sizeof(disabled_model->regs));
	return NULL;
}

static void before(void *fixture)
{
	reset_model(model);
	reset_model(disabled_model);
}

static void after(void *fixture)
{
	struct k_work_sync sync;

	model->storm = false;
	disabled_model->storm = false;
	k_work_cancel_delayable_sync(&model->conversion, &sync);
	k_work_cancel_delayable_sync(&model->unrelated, &sync);
	k_work_cancel_delayable_sync(&disabled_model->conversion, &sync);
	k_work_cancel_delayable_sync(&disabled_model->unrelated, &sync);
}

static void fetch_fresh(const struct device *dev)
{
	struct sensor_value val;

	zassert_ok(sensor_sample_fetch(dev));
	zassert_ok(sensor_channel_get(dev, SENSOR_CHAN_GAUGE_VOLTAGE, &val));
	zassert_equal(sensor_value_to_milli(&val), 3123);
}

ZTEST(bq25798, test_initial_configuration)
{
	zassert_equal(model->initial_regs[BQ25798_REG_MPPT_CONTROL], 0x88);
	zassert_equal(model->initial_regs[BQ25798_REG_TERM_CONTROL], 0x05);
	zassert_equal(sys_get_be16(&model->initial_regs[BQ25798_REG_CHARGE_CURRENT_LIM]), 400);
	zassert_equal(model->initial_regs[BQ25798_REG_INPUT_VOLTAGE_LIM], 36);
	zassert_equal(disabled_model->initial_regs[BQ25798_REG_MPPT_CONTROL], 0);
}

ZTEST(bq25798, test_low_battery_then_recovery)
{
	model->regs[BQ25798_REG_CHARGER_STATUS_3] = BQ25798_CHARGER_STATUS_3_VSYS;
	fetch_fresh(charger);
	zassert_equal(model->mppt_writes, 0);
	model->regs[BQ25798_REG_CHARGER_STATUS_3] = 0;
	fetch_fresh(charger);
	zassert_equal(model->regs[BQ25798_REG_MPPT_CONTROL], 0x89);
	zassert_equal(model->mppt_writes, 1);
	fetch_fresh(charger);
	zassert_equal(model->mppt_writes, 1, "Do not restart an active MPPT cycle");
}

ZTEST(bq25798, test_input_loss_then_return)
{
	fetch_fresh(charger);
	/* Hardware clears EN_MPPT on input loss. */
	model->regs[BQ25798_REG_MPPT_CONTROL] &= ~BQ25798_MPPT_CONTROL_MPPT_ENABLE;
	model->regs[BQ25798_REG_CHARGER_STATUS_0] = 0;
	fetch_fresh(charger);
	zassert_equal(model->mppt_writes, 1);
	model->regs[BQ25798_REG_CHARGER_STATUS_0] = 0x09;
	fetch_fresh(charger);
	zassert_equal(model->regs[BQ25798_REG_MPPT_CONTROL], 0x89);
	zassert_equal(model->mppt_writes, 2);
}

ZTEST(bq25798, test_mppt_disabled)
{
	fetch_fresh(disabled);
	zassert_equal(disabled_model->mppt_writes, 0);
	zassert_equal(disabled_model->regs[BQ25798_REG_MPPT_CONTROL], 0);
}

ZTEST(bq25798, test_mppt_eligibility)
{
	model->regs[BQ25798_REG_CHARGER_STATUS_0] = BQ25798_CHARGER_STATUS_0_VBUS_PRESENT;
	fetch_fresh(charger);
	zassert_equal(model->mppt_writes, 0, "Power must be good");
	model->regs[BQ25798_REG_CHARGER_STATUS_0] = 0x09;
	model->regs[BQ25798_REG_CHARGER_STATUS_2] = 0;
	fetch_fresh(charger);
	zassert_equal(model->mppt_writes, 0, "Battery must be present");
	model->regs[BQ25798_REG_CHARGER_STATUS_2] = BQ25798_CHARGER_STATUS_2_VBAT_PRESENT;
	model->regs[BQ25798_REG_CHARGER_CONTROL_0] |= BQ25798_CHARGER_CONTROL_0_EN_ICO;
	fetch_fresh(charger);
	zassert_equal(model->mppt_writes, 0, "Do not override ICO");
	model->regs[BQ25798_REG_CHARGER_CONTROL_0] &= ~BQ25798_CHARGER_CONTROL_0_EN_ICO;
	model->regs[BQ25798_REG_CHARGER_CONTROL_4] |= BQ25798_CHARGER_CONTROL_4_FORCE_VINDPM_DET;
	fetch_fresh(charger);
	zassert_equal(model->mppt_writes, 0, "Do not override VINDPM detection");
}

ZTEST(bq25798, test_hiz_retry_policy)
{
	model->regs[BQ25798_REG_CHARGER_CONTROL_0] |= BQ25798_CHARGER_CONTROL_0_EN_HIZ;
	fetch_fresh(charger);
	zassert_equal(model->mppt_writes,
		      IS_ENABLED(CONFIG_BQ25798_FETCH_POOR_SOURCE_RETRY) ? 1 : 0);
}

ZTEST(bq25798, test_rejected_enable_retried)
{
	model->reject_enable = true;
	fetch_fresh(charger);
	zassert_equal(model->regs[BQ25798_REG_MPPT_CONTROL], 0x88);
	zassert_equal(model->mppt_reads, 2, "Read back the enable attempt");
	model->reject_enable = false;
	fetch_fresh(charger);
	zassert_equal(model->regs[BQ25798_REG_MPPT_CONTROL], 0x89);
	zassert_equal(model->mppt_writes, 2);
}

ZTEST(bq25798, test_mppt_bus_failure)
{
	model->fail_write = BQ25798_REG_MPPT_CONTROL;
	zassert_equal(sensor_sample_fetch(charger), -EIO);
	zassert_equal(model->adc_reads, 0);
	model->fail_write = -1;
	fetch_fresh(charger);
	zassert_equal(model->regs[BQ25798_REG_MPPT_CONTROL], 0x89);
}

ZTEST(bq25798, test_unrelated_interrupt)
{
	model->spurious_irq = true;
	fetch_fresh(charger);
	zassert_false(model->converting);
	zassert_equal(model->adc_reads, 1);
}

ZTEST(bq25798, test_stale_done_flag)
{
	model->regs[BQ25798_REG_CHARGER_FLAG_2] = BQ25798_CHARGER_FLAG_2_ADC_DONE;
	model->spurious_irq = true;
	fetch_fresh(charger);
	zassert_false(model->converting);
}

ZTEST(bq25798, test_missing_interrupt)
{
	model->completion_irq = false;
	fetch_fresh(charger);
}

ZTEST(bq25798, test_interrupt_storm_timeout)
{
	int64_t start = k_uptime_get();

	model->complete = false;
	model->storm = true;
	zassert_equal(sensor_sample_fetch(charger), -EIO);
	zassert_between_inclusive(k_uptime_get() - start, 500, 600);
	zassert_equal(model->adc_reads, 0);
}

ZTEST(bq25798, test_flag_read_failure)
{
	model->fail_read = BQ25798_REG_CHARGER_FLAG_2;
	zassert_equal(sensor_sample_fetch(charger), -EIO);
	zassert_false(model->converting);
	zassert_equal(model->adc_reads, 0);
}

ZTEST(bq25798, test_completion_read_failure)
{
	model->fail_during_conversion = true;
	zassert_equal(sensor_sample_fetch(charger), -EIO);
	zassert_equal(model->adc_reads, 0);
}

ZTEST_SUITE(bq25798, NULL, setup, before, after, NULL);
