/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#define DT_DRV_COMPAT epeas_aem15820

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/slist.h>

#include <infuse/drivers/mfd/aem15820.h>

#include <zephyr/logging/log.h>

#define AEM15820_IRQ_SUPPORT DT_ANY_INST_HAS_PROP_STATUS_OKAY(int_gpios)

/* Configuration registers span VERSION (0x00) to CTRL (0x17) */
#define AEM15820_WRITE_BURST_MAX (AEM15820_REG_CTRL + 1)

/* Retry delay for reading the IRQ flags after a bus error */
#define AEM15820_IRQ_RETRY K_MSEC(10)

/* Polling interval and timeout for CTRL.SYNCBUSY */
#define AEM15820_SYNC_POLL    K_MSEC(1)
#define AEM15820_SYNC_TIMEOUT K_MSEC(50)

/* IRQENx reset values */
#define AEM15820_IRQ_EN_RESET AEM15820_IRQ_I2CRDY

struct aem15820_config {
	struct i2c_dt_spec bus;
#if AEM15820_IRQ_SUPPORT
	struct gpio_dt_spec int_gpio;
#endif
};

struct aem15820_data {
	/* Serialises bus access and the cached IRQ enables */
	struct k_mutex lock;
	/* Cached value of IRQEN0 | (IRQEN1 << 8) */
	uint16_t irq_en;
	/* Configuration has been applied from the I2C registers since the last reset */
	bool i2c_config;
#if AEM15820_IRQ_SUPPORT
	/* IRQ enables need to be restored after RESET STATE */
	bool irq_restore_pending;
	const struct device *dev;
	struct gpio_callback gpio_cb;
	struct k_work_delayable irq_work;
	/* Protects the callback list, separate from the bus lock so handlers can access the bus */
	struct k_mutex cb_lock;
	sys_slist_t callbacks;
#endif
};

/* PN0 to PN4 */
static const uint8_t aem15820_part_number[] = {'0', '2', '8', '5', '1'};

LOG_MODULE_REGISTER(aem15820, CONFIG_MFD_LOG_LEVEL);

int aem15820_reg_read_burst(const struct device *dev, uint8_t reg, void *buf, size_t len)
{
	const struct aem15820_config *config = dev->config;
	struct aem15820_data *data = dev->data;
	int rc;

	k_mutex_lock(&data->lock, K_FOREVER);
	rc = i2c_write_read_dt(&config->bus, &reg, sizeof(reg), buf, len);
	k_mutex_unlock(&data->lock);
	return rc;
}

int aem15820_reg_read(const struct device *dev, uint8_t reg, uint8_t *val)
{
	return aem15820_reg_read_burst(dev, reg, val, 1);
}

int aem15820_reg_write_burst(const struct device *dev, uint8_t reg, const void *buf, size_t len)
{
	const struct aem15820_config *config = dev->config;
	struct aem15820_data *data = dev->data;
	uint8_t tx[1 + AEM15820_WRITE_BURST_MAX];
	int rc;

	if ((len == 0) || (len > AEM15820_WRITE_BURST_MAX)) {
		return -EINVAL;
	}
	tx[0] = reg;
	memcpy(tx + 1, buf, len);

	k_mutex_lock(&data->lock, K_FOREVER);
	rc = i2c_write_dt(&config->bus, tx, 1 + len);
	k_mutex_unlock(&data->lock);
	return rc;
}

int aem15820_reg_write(const struct device *dev, uint8_t reg, uint8_t val)
{
	return aem15820_reg_write_burst(dev, reg, &val, 1);
}

int aem15820_reg_update(const struct device *dev, uint8_t reg, uint8_t mask, uint8_t val)
{
	struct aem15820_data *data = dev->data;
	uint8_t current;
	int rc;

	/* Mutex is recursive, hold it across the read and write */
	k_mutex_lock(&data->lock, K_FOREVER);
	rc = aem15820_reg_read(dev, reg, &current);
	if (rc == 0) {
		rc = aem15820_reg_write(dev, reg, (current & ~mask) | (val & mask));
	}
	k_mutex_unlock(&data->lock);
	return rc;
}

int aem15820_config_apply(const struct device *dev)
{
	struct aem15820_data *data = dev->data;
	k_timepoint_t timeout = sys_timepoint_calc(AEM15820_SYNC_TIMEOUT);
	uint8_t ctrl;
	int rc;

	k_mutex_lock(&data->lock, K_FOREVER);
	rc = aem15820_reg_write(dev, AEM15820_REG_CTRL, AEM15820_CTRL_UPDATE);
	if (rc != 0) {
		goto end;
	}
	while (true) {
		rc = aem15820_reg_read(dev, AEM15820_REG_CTRL, &ctrl);
		if (rc != 0) {
			goto end;
		}
		if (!(ctrl & AEM15820_CTRL_SYNCBUSY)) {
			break;
		}
		if (sys_timepoint_expired(timeout)) {
			LOG_ERR("Register synchronisation timed out");
			rc = -ETIMEDOUT;
			goto end;
		}
		k_sleep(AEM15820_SYNC_POLL);
	}
	if (!(ctrl & AEM15820_CTRL_UPDATE)) {
		/* Device has not switched to I2C configuration */
		LOG_ERR("Configuration not applied (CTRL %02X)", ctrl);
		rc = -EIO;
		goto end;
	}
	data->i2c_config = true;
end:
	k_mutex_unlock(&data->lock);
	return rc;
}

static int aem15820_irq_en_write(const struct device *dev, uint16_t irq_en)
{
	uint8_t regs[2];

	sys_put_le16(irq_en, regs);
	return aem15820_reg_write_burst(dev, AEM15820_REG_IRQEN0, regs, sizeof(regs));
}

int aem15820_irq_enable(const struct device *dev, uint16_t events, bool enable)
{
	struct aem15820_data *data = dev->data;
	uint16_t irq_en;
	int rc = 0;

	k_mutex_lock(&data->lock, K_FOREVER);
	irq_en = enable ? (data->irq_en | events) : (data->irq_en & ~events);
	if (irq_en != data->irq_en) {
		rc = aem15820_irq_en_write(dev, irq_en);
		if (rc == 0) {
			data->irq_en = irq_en;
			/* Enables are only applied on a configuration update */
			if (data->i2c_config) {
				rc = aem15820_config_apply(dev);
			}
		}
	}
	k_mutex_unlock(&data->lock);
	return rc;
}

#if AEM15820_IRQ_SUPPORT

int aem15820_irq_callback_add(const struct device *dev, struct aem15820_irq_callback *cb)
{
	const struct aem15820_config *config = dev->config;
	struct aem15820_data *data = dev->data;

	if (config->int_gpio.port == NULL) {
		return -ENOTSUP;
	}
	if (cb->handler == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&data->cb_lock, K_FOREVER);
	(void)sys_slist_find_and_remove(&data->callbacks, &cb->node);
	sys_slist_append(&data->callbacks, &cb->node);
	k_mutex_unlock(&data->cb_lock);
	return 0;
}

int aem15820_irq_callback_remove(const struct device *dev, struct aem15820_irq_callback *cb)
{
	struct aem15820_data *data = dev->data;
	bool removed;

	k_mutex_lock(&data->cb_lock, K_FOREVER);
	removed = sys_slist_find_and_remove(&data->callbacks, &cb->node);
	k_mutex_unlock(&data->cb_lock);
	return removed ? 0 : -EINVAL;
}

static void aem15820_irq_work(struct k_work *work)
{
	struct aem15820_data *data =
		CONTAINER_OF(k_work_delayable_from_work(work), struct aem15820_data, irq_work);
	const struct device *dev = data->dev;
	const struct aem15820_config *config = dev->config;
	struct aem15820_irq_callback *cb, *tmp;
	uint8_t flags[2];
	uint16_t events;
	bool restore_retry = false;
	int rc;

	/* Reading the flag registers clears them and releases the IRQ line */
	rc = aem15820_reg_read_burst(dev, AEM15820_REG_IRQFLG0, flags, sizeof(flags));
	if (rc != 0) {
		/* The line stays asserted until the flags are read, no new edge will arrive */
		LOG_WRN("IRQ flag read failed (%d)", rc);
		k_work_reschedule(&data->irq_work, AEM15820_IRQ_RETRY);
		return;
	}
	events = sys_get_le16(flags);
	LOG_DBG("IRQ events: %04X", events);
	if (events & AEM15820_IRQ_I2CRDY) {
		/* All registers have returned to their reset values and the device is configured
		 * from the pins again. Restore the IRQ enables, children re-apply the
		 * configuration.
		 */
		LOG_INF("Device left RESET STATE");
		k_mutex_lock(&data->lock, K_FOREVER);
		data->i2c_config = false;
		data->irq_restore_pending = data->irq_en != AEM15820_IRQ_EN_RESET;
		k_mutex_unlock(&data->lock);
	}

	/* A failed restore is retried even though reading I2CRDY cleared the IRQ line. If a child
	 * applied its configuration between attempts, apply again so the restored enables take
	 * effect.
	 */
	k_mutex_lock(&data->lock, K_FOREVER);
	if (data->irq_restore_pending) {
		rc = aem15820_irq_en_write(dev, data->irq_en);
		if ((rc == 0) && data->i2c_config) {
			rc = aem15820_config_apply(dev);
		}
		if (rc == 0) {
			data->irq_restore_pending = false;
		} else {
			LOG_ERR("Failed to restore IRQ enables (%d)", rc);
			restore_retry = true;
		}
		k_mutex_unlock(&data->lock);
	} else {
		k_mutex_unlock(&data->lock);
	}

	if (events != 0) {
		k_mutex_lock(&data->cb_lock, K_FOREVER);
		SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&data->callbacks, cb, tmp, node) {
			if (cb->events & events) {
				cb->handler(dev, cb, cb->events & events);
			}
		}
		k_mutex_unlock(&data->cb_lock);
	}

	/* An event raised after the flag read re-asserts the line, handle a missed edge */
	if (gpio_pin_get_dt(&config->int_gpio) > 0) {
		k_work_reschedule(&data->irq_work, K_NO_WAIT);
	} else if (restore_retry) {
		k_work_reschedule(&data->irq_work, AEM15820_IRQ_RETRY);
	}
}

static void aem15820_gpio_callback(const struct device *port, struct gpio_callback *cb,
				   uint32_t pins)
{
	struct aem15820_data *data = CONTAINER_OF(cb, struct aem15820_data, gpio_cb);

	ARG_UNUSED(port);
	ARG_UNUSED(pins);

	k_work_reschedule(&data->irq_work, K_NO_WAIT);
}

static int aem15820_irq_init(const struct device *dev)
{
	const struct aem15820_config *config = dev->config;
	struct aem15820_data *data = dev->data;
	int rc;

	data->dev = dev;
	k_mutex_init(&data->cb_lock);
	sys_slist_init(&data->callbacks);
	k_work_init_delayable(&data->irq_work, aem15820_irq_work);

	if (config->int_gpio.port == NULL) {
		return 0;
	}
	if (!gpio_is_ready_dt(&config->int_gpio)) {
		return -ENODEV;
	}
	rc = gpio_pin_configure_dt(&config->int_gpio, GPIO_INPUT);
	if (rc != 0) {
		return rc;
	}
	gpio_init_callback(&data->gpio_cb, aem15820_gpio_callback, BIT(config->int_gpio.pin));
	rc = gpio_add_callback_dt(&config->int_gpio, &data->gpio_cb);
	if (rc != 0) {
		return rc;
	}
	rc = gpio_pin_interrupt_configure_dt(&config->int_gpio, GPIO_INT_EDGE_TO_ACTIVE);
	if (rc != 0) {
		return rc;
	}
	/* Clear events latched before the interrupt was configured (I2CRDY after cold start) */
	k_work_reschedule(&data->irq_work, K_NO_WAIT);
	return 0;
}

#else

int aem15820_irq_callback_add(const struct device *dev, struct aem15820_irq_callback *cb)
{
	return -ENOTSUP;
}

int aem15820_irq_callback_remove(const struct device *dev, struct aem15820_irq_callback *cb)
{
	return -EINVAL;
}

#endif /* AEM15820_IRQ_SUPPORT */

static int aem15820_init(const struct device *dev)
{
	const struct aem15820_config *config = dev->config;
	struct aem15820_data *data = dev->data;
	uint8_t pn[sizeof(aem15820_part_number)];
	uint8_t regs[3];
	int rc;

	k_mutex_init(&data->lock);

	if (!i2c_is_ready_dt(&config->bus)) {
		return -ENODEV;
	}

	/* Any transaction re-enables the interface if SDA and SCL were low during SENSE STO */
	rc = aem15820_reg_read_burst(dev, AEM15820_REG_PN0, pn, sizeof(pn));
	if (rc != 0) {
		LOG_ERR("Reg 0x%02X %s error (%d)", AEM15820_REG_PN0, "read", rc);
		return rc;
	}
	if (memcmp(pn, aem15820_part_number, sizeof(pn)) != 0) {
		LOG_ERR("Unexpected part number %02X%02X%02X%02X%02X", pn[0], pn[1], pn[2], pn[3],
			pn[4]);
		return -ENODEV;
	}
	rc = aem15820_reg_read(dev, AEM15820_REG_VERSION, &regs[0]);
	if (rc != 0) {
		LOG_ERR("Reg 0x%02X %s error (%d)", AEM15820_REG_VERSION, "read", rc);
		return rc;
	}
	LOG_DBG("Version %02X", regs[0]);

	/* IRQEN0, IRQEN1, CTRL */
	rc = aem15820_reg_read_burst(dev, AEM15820_REG_IRQEN0, regs, sizeof(regs));
	if (rc != 0) {
		LOG_ERR("Reg 0x%02X %s error (%d)", AEM15820_REG_IRQEN0, "read", rc);
		return rc;
	}
	data->irq_en = sys_get_le16(regs);
	/* Configuration may have been applied before an application reset */
	data->i2c_config = regs[2] & AEM15820_CTRL_UPDATE;
	LOG_DBG("IRQEN %04X CTRL %02X", data->irq_en, regs[2]);

#if AEM15820_IRQ_SUPPORT
	rc = aem15820_irq_init(dev);
	if (rc != 0) {
		LOG_ERR("IRQ init failed (%d)", rc);
		return rc;
	}
#endif
	return 0;
}

#define AEM15820_DEFINE(inst)                                                                      \
	static struct aem15820_data aem15820_data_##inst;                                          \
	static const struct aem15820_config aem15820_config_##inst = {                             \
		.bus = I2C_DT_SPEC_INST_GET(inst),                                                 \
		IF_ENABLED(AEM15820_IRQ_SUPPORT,                                                   \
			   (.int_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, int_gpios, {0}), ))};       \
	DEVICE_DT_INST_DEFINE(inst, aem15820_init, NULL, &aem15820_data_##inst,                    \
			      &aem15820_config_##inst, POST_KERNEL,                                \
			      CONFIG_MFD_AEM15820_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(AEM15820_DEFINE)
