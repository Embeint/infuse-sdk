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

/* Latch a flag bit, and pulse nINT only if the matching mask bit is clear. Flags latch even
 * when masked, as on hardware. Returns whether nINT was pulsed.
 */
static bool raise_flag(struct charger_emul *data, uint8_t flag_reg, uint8_t bit)
{
	uint8_t mask_reg = BQ25798_REG_CHARGER_MASK_0 + (flag_reg - BQ25798_REG_CHARGER_FLAG_0);

	data->regs[flag_reg] |= bit;
	if (data->regs[mask_reg] & bit) {
		return false;
	}
	pulse_irq(data);
	return true;
}

static void conversion_done(struct k_work *work)
{
	struct charger_emul *data =
		CONTAINER_OF(k_work_delayable_from_work(work), struct charger_emul, conversion);

	/* Only publish the new ADC value once conversion has completed. */
	sys_put_be16(3123, &data->regs[BQ25798_REG_VBAT_ADC]);
	data->converting = false;
	if (data->completion_irq) {
		(void)raise_flag(data, BQ25798_REG_CHARGER_FLAG_2, BQ25798_CHARGER_FLAG_2_ADC_DONE);
	} else {
		data->regs[BQ25798_REG_CHARGER_FLAG_2] |= BQ25798_CHARGER_FLAG_2_ADC_DONE;
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

static int transfer(const struct emul *target, struct i2c_msg *msgs, int num_msgs, int addr)
{
	struct charger_emul *data = target->data;
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
		/* Flag registers are read-to-clear, unlike the status registers. */
		for (int flag = BQ25798_REG_CHARGER_FLAG_0; flag <= BQ25798_REG_CHARGER_FLAG_2;
		     flag++) {
			if ((reg <= flag) && (reg + msgs[1].len > flag)) {
				data->regs[flag] = 0;
			}
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

static int emul_init(const struct emul *target, const struct device *parent)
{
	struct charger_emul *data = target->data;

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
	data->regs[BQ25798_REG_CHARGER_FLAG_0] = 0;
	data->regs[BQ25798_REG_CHARGER_FLAG_1] = 0;
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
	/* Let any MPPT work queued by the driver finish before the next test resets the model */
	k_work_queue_drain(&k_sys_work_q, false);
}

/* Charger event (input, power good, VSYSMIN...): pulse nINT and let the driver work run */
static void charger_event(struct charger_emul *data)
{
	pulse_irq(data);
	k_work_queue_drain(&k_sys_work_q, false);
}

/* Charger event through a specific flag, honouring the configured interrupt masks */
static bool flag_event(struct charger_emul *data, uint8_t flag_reg, uint8_t bit)
{
	bool pulsed = raise_flag(data, flag_reg, bit);

	k_work_queue_drain(&k_sys_work_q, false);
	return pulsed;
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

ZTEST(bq25798, test_interrupt_masks)
{
	/* Only power good, VBAT present, ICO, VSYSMIN and ADC_DONE (plus faults) raise nINT */
	zassert_equal(model->initial_regs[BQ25798_REG_CHARGER_MASK_0],
		      (uint8_t)~BQ25798_CHARGER_MASK_0_PG);
	zassert_equal(model->initial_regs[BQ25798_REG_CHARGER_MASK_1],
		      BQ25798_CHARGER_MASK_1_CHG | BQ25798_CHARGER_MASK_1_VBUS |
			      BQ25798_CHARGER_MASK_1_TREG | BQ25798_CHARGER_MASK_1_BC12_DONE);
	zassert_equal(model->initial_regs[BQ25798_REG_CHARGER_MASK_2],
		      BQ25798_CHARGER_MASK_2_DPDM_DONE | BQ25798_CHARGER_MASK_2_CHG_TMR |
			      BQ25798_CHARGER_MASK_2_TRICH_TMR | BQ25798_CHARGER_MASK_2_PRECHG_TMR |
			      BQ25798_CHARGER_MASK_2_TOPOFF_TMR);
	zassert_equal(model->initial_regs[BQ25798_REG_CHARGER_MASK_3], 0x0F);
	zassert_equal(model->initial_regs[BQ25798_REG_FAULT_MASK_0], 0, "Faults stay unmasked");
	zassert_equal(model->initial_regs[BQ25798_REG_FAULT_MASK_1], 0, "Faults stay unmasked");
}

ZTEST(bq25798, test_adc_channels)
{
	uint8_t disable_0 = BQ25798_ADC_FUNC_DISABLE_0_IBUS | BQ25798_ADC_FUNC_DISABLE_0_VSYS;

	if (!IS_ENABLED(CONFIG_BQ25798_TS_ADC)) {
		disable_0 |= BQ25798_ADC_FUNC_DISABLE_0_TS;
	}
	zassert_equal(model->initial_regs[BQ25798_REG_ADC_FUNC_DISABLE_0], disable_0);
	/* VBUS stays enabled, VAC1 and VAC2 are never reported */
	zassert_equal(model->initial_regs[BQ25798_REG_ADC_FUNC_DISABLE_1],
		      BQ25798_ADC_FUNC_DISABLE_1_DP | BQ25798_ADC_FUNC_DISABLE_1_DM |
			      BQ25798_ADC_FUNC_DISABLE_1_VAC2 | BQ25798_ADC_FUNC_DISABLE_1_VAC1);
}

ZTEST(bq25798, test_adc_conversion_mode)
{
	model->completion_irq = false;
	fetch_fresh(charger);
	zassert_equal(model->regs[BQ25798_REG_ADC_CONTROL], BQ25798_ADC_CONTROL_EN |
								    BQ25798_ADC_CONTROL_ONE_SHOT |
								    BQ25798_ADC_CONTROL_13_BIT);
}

ZTEST(bq25798, test_battery_temperature)
{
	struct sensor_value val;
	int rc;

	/* 50% of REGN with the test divider and default NTC is roughly 38 C */
	sys_put_be16(512, &model->regs[BQ25798_REG_TS_ADC]);
	model->completion_irq = false;
	fetch_fresh(charger);
	rc = sensor_channel_get(charger, SENSOR_CHAN_GAUGE_TEMP, &val);
	if (IS_ENABLED(CONFIG_BQ25798_TS_ADC)) {
		zassert_ok(rc);
		zassert_between_inclusive(val.val1, 35, 41);
	} else {
		zassert_equal(rc, -ENOTSUP);
	}
	/* Die temperature does not depend on the TS channel */
	zassert_ok(sensor_channel_get(charger, SENSOR_CHAN_DIE_TEMP, &val));
}

ZTEST(bq25798, test_masked_events_do_not_interrupt)
{
	/* A weak source repeatedly crossing VAC present only changes presence and VBUS status */
	model->regs[BQ25798_REG_MPPT_CONTROL] = 0x88;
	zassert_false(
		flag_event(model, BQ25798_REG_CHARGER_FLAG_0, BQ25798_CHARGER_MASK_0_AC2_PRESENT));
	zassert_false(
		flag_event(model, BQ25798_REG_CHARGER_FLAG_0, BQ25798_CHARGER_MASK_0_VBUS_PRESENT));
	zassert_false(flag_event(model, BQ25798_REG_CHARGER_FLAG_1, BQ25798_CHARGER_MASK_1_VBUS));
	zassert_false(
		flag_event(model, BQ25798_REG_CHARGER_FLAG_0, BQ25798_CHARGER_MASK_0_POORSRC));
	zassert_equal(model->mppt_reads, 0, "Masked events must not run MPPT work");
	zassert_equal(model->mppt_writes, 0);
	/* Masked flags still latch, and are cleared by the next unmasked event */
	zassert_not_equal(model->regs[BQ25798_REG_CHARGER_FLAG_0], 0);
	zassert_true(flag_event(model, BQ25798_REG_CHARGER_FLAG_0, BQ25798_CHARGER_MASK_0_PG));
	zassert_equal(model->regs[BQ25798_REG_CHARGER_FLAG_0], 0);
	zassert_equal(model->regs[BQ25798_REG_CHARGER_FLAG_1], 0);
	zassert_equal(model->regs[BQ25798_REG_MPPT_CONTROL], 0x89);
}

ZTEST(bq25798, test_mppt_recovery_events_interrupt)
{
	static const struct {
		uint8_t reg;
		uint8_t bit;
	} events[] = {
		{BQ25798_REG_CHARGER_FLAG_0, BQ25798_CHARGER_MASK_0_PG},
		{BQ25798_REG_CHARGER_FLAG_1, BQ25798_CHARGER_MASK_1_VBAT_PRESENT},
		{BQ25798_REG_CHARGER_FLAG_1, BQ25798_CHARGER_MASK_1_ICO},
		{BQ25798_REG_CHARGER_FLAG_2, BQ25798_CHARGER_MASK_2_VSYS},
	};

	/* Every event that can make MPPT eligible again must re-enable it */
	for (int i = 0; i < ARRAY_SIZE(events); i++) {
		model->regs[BQ25798_REG_MPPT_CONTROL] = 0x88;
		zassert_true(flag_event(model, events[i].reg, events[i].bit), "event %d", i);
		zassert_equal(model->regs[BQ25798_REG_MPPT_CONTROL], 0x89, "event %d", i);
		model->regs[BQ25798_REG_CHARGER_FLAG_2] = 0;
	}
	zassert_equal(model->mppt_writes, ARRAY_SIZE(events));
}

ZTEST(bq25798, test_low_battery_then_recovery)
{
	model->regs[BQ25798_REG_CHARGER_STATUS_3] = BQ25798_CHARGER_STATUS_3_VSYS;
	charger_event(model);
	zassert_equal(model->mppt_writes, 0);
	model->regs[BQ25798_REG_CHARGER_STATUS_3] = 0;
	charger_event(model);
	zassert_equal(model->regs[BQ25798_REG_MPPT_CONTROL], 0x89);
	zassert_equal(model->mppt_writes, 1);
	charger_event(model);
	zassert_equal(model->mppt_writes, 1, "Do not restart an active MPPT cycle");
}

ZTEST(bq25798, test_input_loss_then_return)
{
	charger_event(model);
	zassert_equal(model->mppt_writes, 1);
	/* Hardware clears EN_MPPT on input loss. */
	model->regs[BQ25798_REG_MPPT_CONTROL] &= ~BQ25798_MPPT_CONTROL_MPPT_ENABLE;
	model->regs[BQ25798_REG_CHARGER_STATUS_0] = 0;
	charger_event(model);
	zassert_equal(model->mppt_writes, 1);
	model->regs[BQ25798_REG_CHARGER_STATUS_0] = 0x09;
	charger_event(model);
	zassert_equal(model->regs[BQ25798_REG_MPPT_CONTROL], 0x89);
	zassert_equal(model->mppt_writes, 2);
}

ZTEST(bq25798, test_mppt_disabled)
{
	charger_event(disabled_model);
	fetch_fresh(disabled);
	k_work_queue_drain(&k_sys_work_q, false);
	zassert_equal(disabled_model->mppt_writes, 0);
	zassert_equal(disabled_model->mppt_reads, 0);
	zassert_equal(disabled_model->regs[BQ25798_REG_MPPT_CONTROL], 0);
}

ZTEST(bq25798, test_mppt_eligibility)
{
	model->regs[BQ25798_REG_CHARGER_STATUS_0] = BQ25798_CHARGER_STATUS_0_VBUS_PRESENT;
	charger_event(model);
	zassert_equal(model->mppt_writes, 0, "Power must be good");
	model->regs[BQ25798_REG_CHARGER_STATUS_0] = 0x09;
	model->regs[BQ25798_REG_CHARGER_STATUS_2] = 0;
	charger_event(model);
	zassert_equal(model->mppt_writes, 0, "Battery must be present");
	model->regs[BQ25798_REG_CHARGER_STATUS_2] = BQ25798_CHARGER_STATUS_2_VBAT_PRESENT;
	model->regs[BQ25798_REG_CHARGER_CONTROL_0] |= BQ25798_CHARGER_CONTROL_0_EN_ICO;
	charger_event(model);
	zassert_equal(model->mppt_writes, 0, "Do not override ICO");
	model->regs[BQ25798_REG_CHARGER_CONTROL_0] &= ~BQ25798_CHARGER_CONTROL_0_EN_ICO;
	model->regs[BQ25798_REG_CHARGER_CONTROL_4] |= BQ25798_CHARGER_CONTROL_4_FORCE_VINDPM_DET;
	charger_event(model);
	zassert_equal(model->mppt_writes, 0, "Do not override VINDPM detection");
}

ZTEST(bq25798, test_hiz_retry_policy)
{
	model->regs[BQ25798_REG_CHARGER_CONTROL_0] |= BQ25798_CHARGER_CONTROL_0_EN_HIZ;
	charger_event(model);
	zassert_equal(model->mppt_writes, 0, "Do not enable MPPT while in HiZ");

	/* Fetch without a completion interrupt, so no MPPT work is queued */
	model->completion_irq = false;
	fetch_fresh(charger);
	zassert_equal(model->mppt_writes, 0, "Fetch must not enable MPPT");
	zassert_equal(model->regs[BQ25798_REG_CHARGER_CONTROL_0] & BQ25798_CHARGER_CONTROL_0_EN_HIZ,
		      IS_ENABLED(CONFIG_BQ25798_FETCH_POOR_SOURCE_RETRY)
			      ? 0
			      : BQ25798_CHARGER_CONTROL_0_EN_HIZ);

	/* Source qualification completing pulses nINT */
	charger_event(model);
	zassert_equal(model->mppt_writes,
		      IS_ENABLED(CONFIG_BQ25798_FETCH_POOR_SOURCE_RETRY) ? 1 : 0);
}

ZTEST(bq25798, test_rejected_enable_retried)
{
	model->reject_enable = true;
	charger_event(model);
	zassert_equal(model->regs[BQ25798_REG_MPPT_CONTROL], 0x88);
	zassert_equal(model->mppt_reads, 2, "Read back the enable attempt");
	model->reject_enable = false;
	charger_event(model);
	zassert_equal(model->regs[BQ25798_REG_MPPT_CONTROL], 0x89);
	zassert_equal(model->mppt_writes, 2);
}

ZTEST(bq25798, test_mppt_bus_failure)
{
	model->fail_write = BQ25798_REG_MPPT_CONTROL;
	charger_event(model);
	zassert_equal(model->regs[BQ25798_REG_MPPT_CONTROL], 0x88);
	/* MPPT failures must not block measurements */
	fetch_fresh(charger);
	k_work_queue_drain(&k_sys_work_q, false);
	zassert_equal(model->adc_reads, 1);
	model->fail_write = -1;
	charger_event(model);
	zassert_equal(model->regs[BQ25798_REG_MPPT_CONTROL], 0x89);
}

ZTEST(bq25798, test_event_clears_input_flags)
{
	model->regs[BQ25798_REG_CHARGER_FLAG_0] = 0xFF;
	model->regs[BQ25798_REG_CHARGER_FLAG_1] = 0xFF;
	model->regs[BQ25798_REG_CHARGER_FLAG_2] = BQ25798_CHARGER_FLAG_2_ADC_DONE;
	charger_event(model);
	zassert_equal(model->regs[BQ25798_REG_CHARGER_FLAG_0], 0);
	zassert_equal(model->regs[BQ25798_REG_CHARGER_FLAG_1], 0);
	zassert_equal(model->regs[BQ25798_REG_CHARGER_FLAG_2], BQ25798_CHARGER_FLAG_2_ADC_DONE,
		      "FLAG_2 belongs to sample fetch");
}

ZTEST(bq25798, test_flag_read_failure_still_restores)
{
	model->fail_read = BQ25798_REG_CHARGER_FLAG_0;
	charger_event(model);
	zassert_equal(model->regs[BQ25798_REG_MPPT_CONTROL], 0x89);
}

ZTEST(bq25798, test_fetch_does_not_touch_mppt)
{
	model->completion_irq = false;
	fetch_fresh(charger);
	zassert_equal(model->mppt_reads, 0);
	zassert_equal(model->mppt_writes, 0);
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
