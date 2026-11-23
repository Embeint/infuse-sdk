/*
 * Copyright (c) 2026 Embeint Holdings Pty Ltd
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */
#define DT_DRV_COMPAT epeas_aem15820

#include <string.h>

#include <zephyr/drivers/charger.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include <infuse/drivers/mfd/aem15820.h>
#include <infuse/drivers/sensor/aem15820.h>

/* Test-only register/IRQ model */
struct aem15820_emul {
	uint8_t regs[256];
	struct gpio_dt_spec irq;
	/* Registers as of the last CTRL.UPDATE */
	uint8_t applied[AEM15820_REG_CTRL];
	bool bad_pn;
	bool ignore_update;
	int fail_read;
	int fail_write;
	int flag_reads;
	int applies;
	/* Number of CTRL reads that report SYNCBUSY after an update */
	int sync_reads;
	int sync_remaining;
};

/* Configuration register reset values, VERSION (0x00) to CTRL (0x17) */
static const uint8_t reset_regs[AEM15820_REG_CTRL + 1] = {
	0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x05, 0x3A, 0x07, 0x07, 0x30, 0x05,
	0x01, 0xD1, 0x18, 0xD1, 0x18, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
};

static void irq_line_set(struct aem15820_emul *data, int level)
{
	if (data->irq.port != NULL) {
		gpio_emul_input_set(data->irq.port, data->irq.pin, level);
	}
}

/* Latch events that are enabled in IRQENx and raise the IRQ line */
static void raise_events(struct aem15820_emul *data, uint16_t events)
{
	uint16_t enabled = sys_get_le16(&data->regs[AEM15820_REG_IRQEN0]);

	events &= enabled;
	if (events == 0) {
		return;
	}
	sys_put_le16(sys_get_le16(&data->regs[AEM15820_REG_IRQFLG0]) | events,
		     &data->regs[AEM15820_REG_IRQFLG0]);
	irq_line_set(data, 1);
}

static int transfer(const struct emul *target, struct i2c_msg *msgs, int num_msgs, int addr)
{
	struct aem15820_emul *data = target->data;
	uint8_t reg = msgs[0].buf[0];

	if (num_msgs == 2) {
		if (reg == data->fail_read) {
			return -EIO;
		}
		if (reg + msgs[1].len > sizeof(data->regs)) {
			return -EINVAL;
		}
		memcpy(msgs[1].buf, &data->regs[reg], msgs[1].len);
		if ((reg <= AEM15820_REG_CTRL) && (reg + msgs[1].len > AEM15820_REG_CTRL) &&
		    (data->regs[AEM15820_REG_CTRL] & AEM15820_CTRL_SYNCBUSY)) {
			if (data->sync_remaining-- <= 1) {
				data->regs[AEM15820_REG_CTRL] &= ~AEM15820_CTRL_SYNCBUSY;
			}
		}
		/* IRQ flags are read-to-clear and release the IRQ line */
		if ((reg <= AEM15820_REG_IRQFLG1) && (reg + msgs[1].len > AEM15820_REG_IRQFLG0)) {
			data->flag_reads++;
			data->regs[AEM15820_REG_IRQFLG0] = 0;
			data->regs[AEM15820_REG_IRQFLG1] = 0;
			irq_line_set(data, 0);
		}
		return 0;
	}
	if ((num_msgs != 1) || (reg + msgs[0].len - 1 > sizeof(data->regs))) {
		return -EINVAL;
	}
	if (reg == data->fail_write) {
		data->fail_write = -1;
		return -EIO;
	}
	memcpy(&data->regs[reg], &msgs[0].buf[1], msgs[0].len - 1);
	if ((reg <= AEM15820_REG_CTRL) && (reg + msgs[0].len - 1 > AEM15820_REG_CTRL)) {
		if (data->ignore_update ||
		    !(data->regs[AEM15820_REG_CTRL] & AEM15820_CTRL_UPDATE)) {
			data->regs[AEM15820_REG_CTRL] = 0;
		} else {
			data->applies++;
			memcpy(data->applied, data->regs, sizeof(data->applied));
			data->sync_remaining = data->sync_reads;
			data->regs[AEM15820_REG_CTRL] =
				AEM15820_CTRL_UPDATE |
				(data->sync_reads ? AEM15820_CTRL_SYNCBUSY : 0);
		}
	}
	return 0;
}

/* Device passes through RESET STATE: registers return to reset values, I2CRDY raised */
static void device_reset(struct aem15820_emul *data)
{
	irq_line_set(data, 0);
	memcpy(data->regs, reset_regs, sizeof(reset_regs));
	data->regs[AEM15820_REG_VERSION] = 0x11;
	data->regs[AEM15820_REG_IRQFLG0] = AEM15820_IRQ_I2CRDY;
	data->regs[AEM15820_REG_IRQFLG1] = 0;
	irq_line_set(data, 1);
}

static int emul_init(const struct emul *target, const struct device *parent)
{
	struct aem15820_emul *data = target->data;
	static const uint8_t pn[] = {0x30, 0x32, 0x38, 0x35, 0x31};

	data->fail_read = -1;
	data->fail_write = -1;
	memcpy(&data->regs[AEM15820_REG_PN0], pn, sizeof(pn));
	if (data->bad_pn) {
		data->regs[AEM15820_REG_PN0] = 0x00;
	}
	/* Reset values: I2CRDY enabled and latched after cold start */
	memcpy(data->regs, reset_regs, sizeof(reset_regs));
	data->regs[AEM15820_REG_VERSION] = 0x11;
	data->regs[AEM15820_REG_IRQFLG0] = AEM15820_IRQ_I2CRDY;
	if (data->irq.port != NULL) {
		int rc = gpio_pin_configure_dt(&data->irq, GPIO_INPUT);

		if (rc != 0) {
			return rc;
		}
	}
	irq_line_set(data, 1);
	return 0;
}

static const struct i2c_emul_api emul_api = {.transfer = transfer};

#define AEM15820_EMUL(inst)                                                                        \
	static struct aem15820_emul emul_data_##inst = {                                           \
		.irq = GPIO_DT_SPEC_INST_GET_OR(inst, int_gpios, {0}),                             \
		.bad_pn = DT_SAME_NODE(DT_DRV_INST(inst), DT_NODELABEL(pmic_bad)),                 \
	};                                                                                         \
	EMUL_DT_INST_DEFINE(inst, emul_init, &emul_data_##inst, NULL, &emul_api, NULL);
DT_INST_FOREACH_STATUS_OKAY(AEM15820_EMUL)

static const struct device *const pmic = DEVICE_DT_GET(DT_NODELABEL(pmic));
static const struct device *const pmic_sensor = DEVICE_DT_GET(DT_NODELABEL(pmic_sensor));
static const struct device *const basic = DEVICE_DT_GET(DT_NODELABEL(pmic_basic));
static const struct device *const basic_sensor = DEVICE_DT_GET(DT_NODELABEL(pmic_basic_sensor));
static const struct device *const bad = DEVICE_DT_GET(DT_NODELABEL(pmic_bad));
static const struct device *const bad_sensor = DEVICE_DT_GET(DT_NODELABEL(pmic_bad_sensor));
static struct aem15820_emul *const model = EMUL_DT_GET(DT_NODELABEL(pmic))->data;
static struct aem15820_emul *const basic_model = EMUL_DT_GET(DT_NODELABEL(pmic_basic))->data;
static const struct device *const charger = DEVICE_DT_GET(DT_NODELABEL(pmic_charger));
static const struct device *const basic_charger = DEVICE_DT_GET(DT_NODELABEL(pmic_basic_charger));

struct test_callback {
	struct aem15820_irq_callback cb;
	struct k_sem sem;
	uint16_t events;
	int calls;
};

static void test_handler(const struct device *dev, struct aem15820_irq_callback *cb,
			 uint16_t events)
{
	struct test_callback *tcb = CONTAINER_OF(cb, struct test_callback, cb);

	zassert_equal(dev, pmic);
	tcb->events |= events;
	tcb->calls++;
	k_sem_give(&tcb->sem);
}

static void test_callback_init(struct test_callback *tcb, uint16_t events)
{
	memset(tcb, 0, sizeof(*tcb));
	tcb->cb.handler = test_handler;
	tcb->cb.events = events;
	k_sem_init(&tcb->sem, 0, K_SEM_MAX_LIMIT);
}

static void fetch_with_regs(const struct device *dev, struct aem15820_emul *m, uint8_t temp,
			    uint8_t sto, uint8_t src)
{
	m->regs[AEM15820_REG_TEMP] = temp;
	m->regs[AEM15820_REG_STO] = sto;
	m->regs[AEM15820_REG_SRC] = src;
	zassert_ok(sensor_sample_fetch(dev));
}

static int32_t channel_micro(const struct device *dev, enum sensor_channel chan)
{
	struct sensor_value val;

	zassert_ok(sensor_channel_get(dev, chan, &val));
	return (int32_t)sensor_value_to_micro(&val);
}

ZTEST(aem15820, test_init)
{
	zassert_true(device_is_ready(pmic));
	zassert_true(device_is_ready(pmic_sensor));
	zassert_true(device_is_ready(basic));
	zassert_true(device_is_ready(basic_sensor));
	zassert_false(device_is_ready(bad));
	zassert_false(device_is_ready(bad_sensor));

	/* I2CRDY latched at cold start is cleared once the IRQ is configured */
	k_sleep(K_MSEC(10));
	zassert_equal(0, model->regs[AEM15820_REG_IRQFLG0]);
	zassert_equal(0, gpio_pin_get_dt(&model->irq));
	/* No IRQ line, flags are left alone */
	zassert_equal(AEM15820_IRQ_I2CRDY, basic_model->regs[AEM15820_REG_IRQFLG0]);
}

ZTEST(aem15820, test_register_access)
{
	uint8_t buf[AEM15820_REG_CTRL + 2] = {0};
	uint8_t val;

	zassert_ok(aem15820_reg_read(pmic, AEM15820_REG_VERSION, &val));
	zassert_equal(0x11, val);

	zassert_ok(aem15820_reg_write(pmic, AEM15820_REG_VOVDIS, 0x0A));
	zassert_equal(0x0A, model->regs[AEM15820_REG_VOVDIS]);

	zassert_ok(aem15820_reg_update(pmic, AEM15820_REG_VOVDIS, 0x03, 0x01));
	zassert_equal(0x09, model->regs[AEM15820_REG_VOVDIS]);

	buf[0] = 0x12;
	buf[1] = 0x34;
	zassert_ok(aem15820_reg_write_burst(pmic, AEM15820_REG_TEMPCOLDCH, buf, 2));
	zassert_ok(aem15820_reg_read_burst(pmic, AEM15820_REG_TEMPCOLDCH, buf + 2, 2));
	zassert_equal(0x12, buf[2]);
	zassert_equal(0x34, buf[3]);

	/* All configuration registers fit in a single burst, but no more */
	zassert_ok(
		aem15820_reg_write_burst(pmic, AEM15820_REG_VERSION, buf, AEM15820_REG_CTRL + 1));
	zassert_equal(-EINVAL,
		      aem15820_reg_write_burst(pmic, AEM15820_REG_VERSION, buf, sizeof(buf)));
	zassert_equal(-EINVAL, aem15820_reg_write_burst(pmic, AEM15820_REG_VERSION, buf, 0));

	model->fail_read = AEM15820_REG_VOVCH;
	zassert_equal(-EIO, aem15820_reg_read(pmic, AEM15820_REG_VOVCH, &val));
	zassert_equal(-EIO, aem15820_reg_update(pmic, AEM15820_REG_VOVCH, 0x01, 0x01));
}

ZTEST(aem15820, test_irq_enable)
{
	zassert_ok(aem15820_irq_enable(pmic, AEM15820_IRQ_STODONE | AEM15820_IRQ_VOVDIS, true));
	zassert_equal(AEM15820_IRQ_I2CRDY | AEM15820_IRQ_VOVDIS, model->regs[AEM15820_REG_IRQEN0]);
	zassert_equal(AEM15820_IRQ_STODONE >> 8, model->regs[AEM15820_REG_IRQEN1]);

	zassert_ok(aem15820_irq_enable(pmic, AEM15820_IRQ_VOVDIS, false));
	zassert_equal(AEM15820_IRQ_I2CRDY, model->regs[AEM15820_REG_IRQEN0]);
	zassert_equal(AEM15820_IRQ_STODONE >> 8, model->regs[AEM15820_REG_IRQEN1]);

	/* Device without an IRQ line can still enable events, but not register callbacks */
	struct test_callback tcb;

	test_callback_init(&tcb, AEM15820_IRQ_STODONE);
	zassert_ok(aem15820_irq_enable(basic, AEM15820_IRQ_STODONE, true));
	zassert_equal(-ENOTSUP, aem15820_irq_callback_add(basic, &tcb.cb));
}

ZTEST(aem15820, test_irq_dispatch)
{
	struct test_callback sto, temp, all;

	test_callback_init(&sto, AEM15820_IRQ_STODONE);
	test_callback_init(&temp, AEM15820_IRQ_TEMPDONE);
	test_callback_init(&all, 0xFFFF);

	zassert_equal(-EINVAL, aem15820_irq_callback_remove(pmic, &sto.cb));
	zassert_ok(aem15820_irq_callback_add(pmic, &sto.cb));
	zassert_ok(aem15820_irq_callback_add(pmic, &temp.cb));
	zassert_ok(aem15820_irq_callback_add(pmic, &all.cb));
	/* Re-adding does not duplicate the callback */
	zassert_ok(aem15820_irq_callback_add(pmic, &sto.cb));

	zassert_ok(aem15820_irq_enable(pmic, AEM15820_IRQ_STODONE | AEM15820_IRQ_TEMPDONE, true));

	raise_events(model, AEM15820_IRQ_STODONE);
	zassert_ok(k_sem_take(&sto.sem, K_MSEC(100)));
	zassert_ok(k_sem_take(&all.sem, K_MSEC(100)));
	zassert_equal(AEM15820_IRQ_STODONE, sto.events);
	zassert_equal(AEM15820_IRQ_STODONE, all.events);
	zassert_equal(1, sto.calls);
	zassert_equal(0, temp.calls);
	zassert_equal(0, gpio_pin_get_dt(&model->irq));

	/* Disabled events never assert the line */
	raise_events(model, AEM15820_IRQ_APMDONE);
	k_sleep(K_MSEC(20));
	zassert_equal(1, all.calls);

	/* Multiple events in one assertion are delivered together */
	all.events = 0;
	raise_events(model, AEM15820_IRQ_STODONE | AEM15820_IRQ_TEMPDONE);
	zassert_ok(k_sem_take(&temp.sem, K_MSEC(100)));
	zassert_ok(k_sem_take(&all.sem, K_MSEC(100)));
	zassert_equal(AEM15820_IRQ_TEMPDONE, temp.events);
	zassert_equal(AEM15820_IRQ_STODONE | AEM15820_IRQ_TEMPDONE, all.events);
	zassert_equal(2, sto.calls);

	/* Removed callbacks are no longer run */
	zassert_ok(aem15820_irq_callback_remove(pmic, &sto.cb));
	raise_events(model, AEM15820_IRQ_STODONE);
	zassert_ok(k_sem_take(&all.sem, K_MSEC(100)));
	zassert_equal(2, sto.calls);

	zassert_ok(aem15820_irq_callback_remove(pmic, &temp.cb));
	zassert_ok(aem15820_irq_callback_remove(pmic, &all.cb));
}

ZTEST(aem15820, test_irq_bus_error_retry)
{
	struct test_callback sto;
	int reads;

	test_callback_init(&sto, AEM15820_IRQ_STODONE);
	zassert_ok(aem15820_irq_callback_add(pmic, &sto.cb));
	zassert_ok(aem15820_irq_enable(pmic, AEM15820_IRQ_STODONE, true));

	/* Flag read fails, line stays asserted */
	model->fail_read = AEM15820_REG_IRQFLG0;
	reads = model->flag_reads;
	raise_events(model, AEM15820_IRQ_STODONE);
	zassert_equal(-EAGAIN, k_sem_take(&sto.sem, K_MSEC(25)));
	zassert_equal(1, gpio_pin_get_dt(&model->irq));

	/* Retried without a new edge once the bus recovers */
	model->fail_read = -1;
	zassert_ok(k_sem_take(&sto.sem, K_MSEC(100)));
	zassert_equal(reads + 1, model->flag_reads);
	zassert_equal(0, gpio_pin_get_dt(&model->irq));

	zassert_ok(aem15820_irq_callback_remove(pmic, &sto.cb));
}

ZTEST(aem15820, test_sensor_sto)
{
	/* VSTO = 4.8 * DATA / 256 */
	fetch_with_regs(pmic_sensor, model, 0x50, 0xBB, 0x20);
	zassert_equal(3506250, channel_micro(pmic_sensor, SENSOR_CHAN_VOLTAGE));
	zassert_equal(3506250, channel_micro(pmic_sensor, SENSOR_CHAN_GAUGE_VOLTAGE));

	fetch_with_regs(pmic_sensor, model, 0x50, 0x00, 0x20);
	zassert_equal(0, channel_micro(pmic_sensor, SENSOR_CHAN_VOLTAGE));
	fetch_with_regs(pmic_sensor, model, 0x50, 0xFF, 0x20);
	zassert_equal(4781250, channel_micro(pmic_sensor, SENSOR_CHAN_VOLTAGE));

	/* Values are latched at fetch */
	model->regs[AEM15820_REG_STO] = 0x10;
	zassert_equal(4781250, channel_micro(pmic_sensor, SENSOR_CHAN_VOLTAGE));
}

ZTEST(aem15820, test_sensor_src)
{
	/* Datasheet Table 75 lookup values (mV) */
	static const struct {
		uint8_t code;
		int32_t mv;
	} table[] = {
		{0x00, 113},  {0x06, 113},  {0x07, 128},  {0x12, 293},  {0x13, 315},
		{0x20, 705},  {0x39, 1455}, {0x68, 1410}, {0x6D, 1634}, {0x79, 2172},
		{0x9F, 2045}, {0xA2, 2318}, {0xAF, 3500}, {0xB9, 4409},
	};
	static const uint8_t invalid[] = {0x3A, 0x67, 0x7A, 0x9E, 0xBA, 0xFF};
	struct sensor_value val;
	int32_t uv;

	for (int i = 0; i < ARRAY_SIZE(table); i++) {
		fetch_with_regs(pmic_sensor, model, 0x50, 0xBB, table[i].code);
		uv = channel_micro(pmic_sensor,
				   (enum sensor_channel)SENSOR_CHAN_AEM15820_SRC_VOLTAGE);
		/* Table is rounded to the nearest mV */
		zassert_within(table[i].mv * 1000, uv, 500, "code %02X: %d uV", table[i].code, uv);
	}
	for (int i = 0; i < ARRAY_SIZE(invalid); i++) {
		fetch_with_regs(pmic_sensor, model, 0x50, 0xBB, invalid[i]);
		zassert_equal(-EIO,
			      sensor_channel_get(
				      pmic_sensor,
				      (enum sensor_channel)SENSOR_CHAN_AEM15820_SRC_VOLTAGE, &val));
	}
}

ZTEST(aem15820, test_sensor_temperature)
{
	struct sensor_value val;

	/* RTH = RDIV * DATA / (256 - DATA), 80 -> 10k -> 25C */
	fetch_with_regs(pmic_sensor, model, 80, 0xBB, 0x20);
	zassert_within(25000000, channel_micro(pmic_sensor, SENSOR_CHAN_GAUGE_TEMP), 10000);

	/* Expected values from the datasheet equations, R0 = 10k, B = 3380, RDIV = 22k */
	fetch_with_regs(pmic_sensor, model, 128, 0xBB, 0x20);
	zassert_within(5612000, channel_micro(pmic_sensor, SENSOR_CHAN_GAUGE_TEMP), 10000);
	fetch_with_regs(pmic_sensor, model, 0xD1, 0xBB, 0x20);
	zassert_within(-24935000, channel_micro(pmic_sensor, SENSOR_CHAN_GAUGE_TEMP), 10000);
	fetch_with_regs(pmic_sensor, model, 0x18, 0xBB, 0x20);
	zassert_within(69776000, channel_micro(pmic_sensor, SENSOR_CHAN_GAUGE_TEMP), 10000);
	fetch_with_regs(pmic_sensor, model, 0xFF, 0xBB, 0x20);
	zassert_within(-81825000, channel_micro(pmic_sensor, SENSOR_CHAN_GAUGE_TEMP), 10000);

	/* Shorted thermistor */
	fetch_with_regs(pmic_sensor, model, 0x00, 0xBB, 0x20);
	zassert_equal(-EIO, sensor_channel_get(pmic_sensor, SENSOR_CHAN_GAUGE_TEMP, &val));

	/* No thermistor configured */
	fetch_with_regs(basic_sensor, basic_model, 80, 0xBB, 0x20);
	zassert_equal(-ENOTSUP, sensor_channel_get(basic_sensor, SENSOR_CHAN_GAUGE_TEMP, &val));
	zassert_equal(3506250, channel_micro(basic_sensor, SENSOR_CHAN_VOLTAGE));
}

/* Expected configuration for AEM15820_config.yaml */
static const uint8_t expected_mppt[] = {
	[AEM15820_REG_SRCREGU0] = 0x0D,    /* MPPT, 85% */
	[AEM15820_REG_SRCREGU1] = 0x3E,    /* 233 ms wait, 14895 ms period */
	[AEM15820_REG_VOVDIS] = 0x20,      /* 3.000 V */
	[AEM15820_REG_VCHRDY] = 0x2D,      /* 3.300 V */
	[AEM15820_REG_VOVCH] = 0x50,       /* 4.200 V */
	[AEM15820_REG_BSTLPCFG] = 0x0B,    /* x3, enabled */
	[AEM15820_REG_BSTHPCFG] = 0x13,    /* x6, enabled */
	[AEM15820_REG_BUCKCFG] = 0x15,     /* x2, 1.8 V */
	[AEM15820_REG_VCHRDYBUCK] = 0x2D,  /* 3.300 V */
	[AEM15820_REG_CHG5V] = 0x01,       /* Enabled, CC only */
	[AEM15820_REG_TEMPCOLDCH] = 0xD1,  /* -24.93 C */
	[AEM15820_REG_TEMPHOTCH] = 0x18,   /* 69.78 C */
	[AEM15820_REG_TEMPCOLDDIS] = 0xD1, /* -24.93 C */
	[AEM15820_REG_TEMPHOTDIS] = 0x18,  /* 69.78 C */
	[AEM15820_REG_TEMPPROTECT] = 0x01, /* Enabled */
	[AEM15820_REG_SRCLOW] = 0x03,      /* 0.300 V */
	[AEM15820_REG_APM] = 0x07,         /* BSTLP, BSTHP, LOAD, counter, 233 ms */
	[AEM15820_REG_APMACC] = 0x00,      /* 1 window */
};

static const uint8_t expected_cv[] = {
	[AEM15820_REG_SRCREGU0] = 0x0A,   /* CV, LVL 0x75 (1.500 V) */
	[AEM15820_REG_SRCREGU1] = 0x0E,   /* LVL 0x75 */
	[AEM15820_REG_VOVDIS] = 0x05,     /* 2.494 V */
	[AEM15820_REG_VCHRDY] = 0x0D,     /* 2.700 V */
	[AEM15820_REG_VOVCH] = 0x65,      /* 4.594 V */
	[AEM15820_REG_BSTLPCFG] = 0x07,   /* x2, enabled */
	[AEM15820_REG_BSTHPCFG] = 0x06,   /* x2, disabled */
	[AEM15820_REG_BUCKCFG] = 0x30,    /* x4, off */
	[AEM15820_REG_VCHRDYBUCK] = 0x0D, /* Follows VCHRDY */
	[AEM15820_REG_CHG5V] = 0x5F,      /* Enabled, CV at 4.10 V */
	[AEM15820_REG_TEMPCOLDCH] = 0xD1, /* Reset values */
	[AEM15820_REG_TEMPHOTCH] = 0x18,  [AEM15820_REG_TEMPCOLDDIS] = 0xD1,
	[AEM15820_REG_TEMPHOTDIS] = 0x18, [AEM15820_REG_TEMPPROTECT] = 0x00, /* No thermistor */
	[AEM15820_REG_SRCLOW] = 0x00,                                        /* Unused in CV mode */
	[AEM15820_REG_APM] = 0x3B,    /* BSTLP, BSTHP, CHG5V, power, 116 ms */
	[AEM15820_REG_APMACC] = 0x02, /* 3 windows */
};

static void assert_applied(struct aem15820_emul *m, const uint8_t *expected)
{
	for (int reg = AEM15820_REG_SRCREGU0; reg <= AEM15820_REG_APMACC; reg++) {
		if ((reg == 0x03) || (reg == 0x04)) {
			continue;
		}
		zassert_equal(expected[reg], m->applied[reg], "reg %02X: %02X != %02X", reg,
			      expected[reg], m->applied[reg]);
	}
	zassert_equal(AEM15820_CTRL_UPDATE, m->regs[AEM15820_REG_CTRL]);
}

ZTEST(aem15820, test_charger_config)
{
	zassert_true(device_is_ready(charger));
	zassert_true(device_is_ready(basic_charger));
	zassert_true(model->applies >= 1);
	assert_applied(model, expected_mppt);
	assert_applied(basic_model, expected_cv);
}

ZTEST(aem15820, test_charger_enable)
{
	int applies = model->applies;

	zassert_ok(charger_charge_enable(charger, false));
	zassert_equal(applies + 1, model->applies);
	zassert_equal(0x0A, model->applied[AEM15820_REG_BSTLPCFG]);
	zassert_equal(0x12, model->applied[AEM15820_REG_BSTHPCFG]);
	zassert_equal(0x00, model->applied[AEM15820_REG_CHG5V]);

	zassert_ok(charger_charge_enable(charger, true));
	assert_applied(model, expected_mppt);

	/* Failed apply leaves the state unchanged */
	model->ignore_update = true;
	zassert_equal(-EIO, charger_charge_enable(charger, false));
	model->ignore_update = false;
	zassert_ok(charger_charge_enable(charger, true));
	assert_applied(model, expected_mppt);
}

static void check_props(const struct device *dev, uint8_t status0, uint8_t status1,
			enum charger_online online, enum charger_status status,
			enum charger_health health)
{
	struct aem15820_emul *m = (dev == charger) ? model : basic_model;
	union charger_propval val;

	m->regs[AEM15820_REG_STATUS0] = status0;
	m->regs[AEM15820_REG_STATUS1] = status1;
	zassert_ok(charger_get_prop(dev, CHARGER_PROP_ONLINE, &val));
	zassert_equal(online, val.online);
	zassert_ok(charger_get_prop(dev, CHARGER_PROP_STATUS, &val));
	zassert_equal(status, val.status);
	zassert_ok(charger_get_prop(dev, CHARGER_PROP_HEALTH, &val));
	zassert_equal(health, val.health);
}

ZTEST(aem15820, test_charger_properties)
{
	union charger_propval val;

	check_props(charger, AEM15820_STATUS0_CHRDY, 0, CHARGER_ONLINE_FIXED,
		    CHARGER_STATUS_CHARGING, CHARGER_HEALTH_GOOD);
	check_props(charger, AEM15820_STATUS0_CHRDY | AEM15820_STATUS0_SRCLOW, 0,
		    CHARGER_ONLINE_OFFLINE, CHARGER_STATUS_DISCHARGING, CHARGER_HEALTH_GOOD);
	check_props(charger,
		    AEM15820_STATUS0_CHRDY | AEM15820_STATUS0_SRCLOW | AEM15820_STATUS0_CHG5VCONN,
		    0, CHARGER_ONLINE_FIXED, CHARGER_STATUS_CHARGING, CHARGER_HEALTH_GOOD);
	check_props(charger, AEM15820_STATUS0_CHRDY | AEM15820_STATUS0_OVCH, 0,
		    CHARGER_ONLINE_FIXED, CHARGER_STATUS_FULL, CHARGER_HEALTH_GOOD);
	check_props(charger, 0, AEM15820_STATUS1_TEMPCOLDCH, CHARGER_ONLINE_FIXED,
		    CHARGER_STATUS_CHARGING, CHARGER_HEALTH_COLD);
	check_props(charger, 0, AEM15820_STATUS1_TEMPHOTCH, CHARGER_ONLINE_FIXED,
		    CHARGER_STATUS_CHARGING, CHARGER_HEALTH_HOT);
	/* No thermal protection without a thermistor */
	check_props(basic_charger, 0, AEM15820_STATUS1_TEMPHOTCH, CHARGER_ONLINE_FIXED,
		    CHARGER_STATUS_CHARGING, CHARGER_HEALTH_UNKNOWN);

	zassert_ok(charger_charge_enable(charger, false));
	check_props(charger, 0, 0, CHARGER_ONLINE_FIXED, CHARGER_STATUS_NOT_CHARGING,
		    CHARGER_HEALTH_GOOD);

	zassert_equal(-ENOTSUP, charger_get_prop(charger, CHARGER_PROP_PRESENT, &val));
	model->fail_read = AEM15820_REG_STATUS0;
	zassert_equal(-EIO, charger_get_prop(charger, CHARGER_PROP_STATUS, &val));
}

ZTEST(aem15820, test_charger_reset_recovery)
{
	struct test_callback sto;
	int applies;

	test_callback_init(&sto, AEM15820_IRQ_STODONE);
	zassert_ok(aem15820_irq_callback_add(pmic, &sto.cb));

	/* Enabling events once configured re-applies the configuration */
	applies = model->applies;
	zassert_ok(aem15820_irq_enable(pmic, AEM15820_IRQ_STODONE, true));
	zassert_equal(applies + 1, model->applies);
	zassert_equal(AEM15820_IRQ_STODONE >> 8, model->applied[AEM15820_REG_IRQEN1]);

	/* Device resets, configuration and IRQ enables are restored */
	model->fail_write = AEM15820_REG_IRQEN0;
	device_reset(model);
	k_sleep(K_MSEC(50));
	zassert_equal(AEM15820_IRQ_STODONE >> 8, model->regs[AEM15820_REG_IRQEN1]);
	zassert_equal(AEM15820_IRQ_STODONE >> 8, model->applied[AEM15820_REG_IRQEN1]);
	assert_applied(model, expected_mppt);
	zassert_equal(0, gpio_pin_get_dt(&model->irq));

	/* Events work again */
	raise_events(model, AEM15820_IRQ_STODONE);
	zassert_ok(k_sem_take(&sto.sem, K_MSEC(100)));

	/* Charging disabled before the reset stays disabled */
	zassert_ok(charger_charge_enable(charger, false));
	device_reset(model);
	k_sleep(K_MSEC(20));
	zassert_equal(0x00, model->applied[AEM15820_REG_CHG5V]);

	zassert_ok(aem15820_irq_callback_remove(pmic, &sto.cb));
}

ZTEST(aem15820, test_config_apply)
{
	int applies = model->applies;

	/* Synchronisation takes a few polls */
	model->sync_reads = 3;
	zassert_ok(aem15820_config_apply(pmic));
	zassert_equal(applies + 1, model->applies);
	zassert_equal(AEM15820_CTRL_UPDATE, model->regs[AEM15820_REG_CTRL]);

	/* Synchronisation never completes */
	model->sync_reads = INT_MAX;
	zassert_equal(-ETIMEDOUT, aem15820_config_apply(pmic));
	model->sync_reads = 0;

	/* Device does not switch to I2C configuration */
	model->ignore_update = true;
	zassert_equal(-EIO, aem15820_config_apply(pmic));
	model->ignore_update = false;

	model->fail_read = AEM15820_REG_CTRL;
	zassert_equal(-EIO, aem15820_config_apply(pmic));
}

static void set_apm(struct aem15820_emul *m, uint32_t lp, uint32_t hp, uint32_t load, uint8_t err)
{
	sys_put_le24(lp, &m->regs[AEM15820_REG_APM0BSTLP]);
	sys_put_le24(hp, &m->regs[AEM15820_REG_APM0BSTHP]);
	sys_put_le24(load, &m->regs[AEM15820_REG_APM0LOAD]);
	m->regs[AEM15820_REG_APMERR] = err;
}

#define CHAN_LP   ((enum sensor_channel)SENSOR_CHAN_AEM15820_BOOST_LP_POWER)
#define CHAN_HP   ((enum sensor_channel)SENSOR_CHAN_AEM15820_BOOST_HP_POWER)
#define CHAN_LOAD ((enum sensor_channel)SENSOR_CHAN_AEM15820_LOAD_POWER)

ZTEST(aem15820, test_sensor_apm_counter)
{
	struct sensor_value val;

	/* Counter mode, single window: P = COUNTER * alpha */
	set_apm(model, 0x001234, 0x000010, 0x400000, 0);
	zassert_ok(sensor_sample_fetch(pmic_sensor));
	/* 4660 * 18040 pW = 84.0664 uW */
	zassert_within(84066, channel_micro(pmic_sensor, CHAN_LP), 2);
	/* 16 * 12 uW = 192 uW */
	zassert_within(192000, channel_micro(pmic_sensor, CHAN_HP), 2);
	/* 4194304 * 2265 pW = 9.500099 mW */
	zassert_within(9500099, channel_micro(pmic_sensor, CHAN_LOAD), 10);

	/* Counter is 23 bits */
	set_apm(model, 0x800010, 0x000000, 0x000000, 0);
	zassert_ok(sensor_sample_fetch(pmic_sensor));
	zassert_within(289, channel_micro(pmic_sensor, CHAN_LP), 1);
	zassert_equal(0, channel_micro(pmic_sensor, CHAN_HP));

	/* Overflowed and corrupted counters */
	set_apm(model, 0x10, 0x10, 0x10, BIT(4) | BIT(1));
	zassert_ok(sensor_sample_fetch(pmic_sensor));
	zassert_equal(-EIO, sensor_channel_get(pmic_sensor, CHAN_LP, &val));
	zassert_within(192000, channel_micro(pmic_sensor, CHAN_HP), 2);
	zassert_equal(-ERANGE, sensor_channel_get(pmic_sensor, CHAN_LOAD, &val));
	set_apm(model, 0x10, 0x10, 0x10, BIT(2) | BIT(0));
	zassert_ok(sensor_sample_fetch(pmic_sensor));
	zassert_equal(-ERANGE, sensor_channel_get(pmic_sensor, CHAN_LP, &val));
	zassert_equal(-ERANGE, sensor_channel_get(pmic_sensor, CHAN_HP, &val));

	/* APM read failure fails the fetch */
	model->fail_read = AEM15820_REG_APM0BSTLP;
	zassert_equal(-EIO, sensor_sample_fetch(pmic_sensor));
}

ZTEST(aem15820, test_sensor_apm_power_accumulated)
{
	struct sensor_value val;

	/* Power mode, 3 windows: P = (POWER << OFFSET) * alpha / 3 */
	set_apm(basic_model, (2 << 19) | 4096, (1 << 19) | 300, 0x123456, 0);
	zassert_ok(sensor_sample_fetch(basic_sensor));
	/* 16384 / 3 * 1000 pW = 5.461 uW */
	zassert_within(5461, channel_micro(basic_sensor, CHAN_LP), 1);
	/* High-power boost is always a counter, but the accumulator still applies an offset */
	zassert_within(200, channel_micro(basic_sensor, CHAN_HP), 1);
	/* Monitor not enabled on the charger */
	zassert_equal(-ENOTSUP, sensor_channel_get(basic_sensor, CHAN_LOAD, &val));
}

ZTEST(aem15820, test_sensor_errors)
{
	struct sensor_value val;

	zassert_equal(-ENOTSUP, sensor_channel_get(pmic_sensor, SENSOR_CHAN_CURRENT, &val));

	model->fail_read = AEM15820_REG_TEMP;
	zassert_equal(-EIO, sensor_sample_fetch(pmic_sensor));
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);

	model->fail_read = -1;
	model->fail_write = -1;
	model->ignore_update = false;
	model->sync_reads = 0;
	basic_model->fail_read = -1;
	zassert_ok(charger_charge_enable(charger, true));
	/* Return to the reset IRQ enables */
	zassert_ok(aem15820_irq_enable(pmic, 0xFFFF & ~AEM15820_IRQ_I2CRDY, false));
	zassert_ok(aem15820_irq_enable(basic, 0xFFFF & ~AEM15820_IRQ_I2CRDY, false));
}

ZTEST_SUITE(aem15820, NULL, NULL, before, NULL, NULL);
