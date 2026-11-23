/**
 * @file
 * @brief e-peas AEM15820 energy harvesting PMIC core driver
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_MFD_AEM15820_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_MFD_AEM15820_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/sys/slist.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief e-peas AEM15820 core driver
 * @defgroup aem15820_apis AEM15820 core driver
 * @ingroup infuse_drivers
 *
 * The core driver owns the I2C bus and the IRQ line. Child drivers (sensor, charger) access
 * registers and receive interrupt events exclusively through this API.
 * @{
 */

/** AEM15820 register addresses */
enum aem15820_reg {
	AEM15820_REG_VERSION = 0x00,
	AEM15820_REG_SRCREGU0 = 0x01,
	AEM15820_REG_SRCREGU1 = 0x02,
	AEM15820_REG_VOVDIS = 0x05,
	AEM15820_REG_VCHRDY = 0x06,
	AEM15820_REG_VOVCH = 0x07,
	AEM15820_REG_BSTLPCFG = 0x08,
	AEM15820_REG_BSTHPCFG = 0x09,
	AEM15820_REG_BUCKCFG = 0x0A,
	AEM15820_REG_VCHRDYBUCK = 0x0B,
	AEM15820_REG_CHG5V = 0x0C,
	AEM15820_REG_TEMPCOLDCH = 0x0D,
	AEM15820_REG_TEMPHOTCH = 0x0E,
	AEM15820_REG_TEMPCOLDDIS = 0x0F,
	AEM15820_REG_TEMPHOTDIS = 0x10,
	AEM15820_REG_TEMPPROTECT = 0x11,
	AEM15820_REG_SRCLOW = 0x12,
	AEM15820_REG_APM = 0x13,
	AEM15820_REG_APMACC = 0x14,
	AEM15820_REG_IRQEN0 = 0x15,
	AEM15820_REG_IRQEN1 = 0x16,
	AEM15820_REG_CTRL = 0x17,
	AEM15820_REG_IRQFLG0 = 0x18,
	AEM15820_REG_IRQFLG1 = 0x19,
	AEM15820_REG_STATUS0 = 0x1A,
	AEM15820_REG_STATUS1 = 0x1B,
	AEM15820_REG_APM0BSTLP = 0x1C,
	AEM15820_REG_APM0BSTHP = 0x1F,
	AEM15820_REG_APM0LOAD = 0x22,
	AEM15820_REG_APM0CHG5V = 0x25,
	AEM15820_REG_APMERR = 0x27,
	AEM15820_REG_TEMP = 0x28,
	AEM15820_REG_STO = 0x29,
	AEM15820_REG_SRC = 0x2A,
	AEM15820_REG_PN0 = 0xE0,
	AEM15820_REG_PN4 = 0xE4,
};

/* CTRL register */
#define AEM15820_CTRL_UPDATE   BIT(0)
#define AEM15820_CTRL_SYNCBUSY BIT(2)

/* STATUS0 register */
#define AEM15820_STATUS0_OVDIS     BIT(0)
#define AEM15820_STATUS0_CHRDY     BIT(1)
#define AEM15820_STATUS0_OVCH      BIT(2)
#define AEM15820_STATUS0_SRCLOW    BIT(3)
#define AEM15820_STATUS0_CHG5VCONN BIT(5)

/* STATUS1 register */
#define AEM15820_STATUS1_TEMPCOLDCH  BIT(0)
#define AEM15820_STATUS1_TEMPHOTCH   BIT(1)
#define AEM15820_STATUS1_TEMPCOLDDIS BIT(2)
#define AEM15820_STATUS1_TEMPHOTDIS  BIT(3)

/**
 * @name IRQ event bits
 *
 * Combined view of IRQEN0/IRQFLG0 (bits 0-7) and IRQEN1/IRQFLG1 (bits 8-15).
 * @{
 */
/** Serial interface ready (device left RESET STATE, all registers at defaults) */
#define AEM15820_IRQ_I2CRDY       BIT(0)
/** Storage element crossed the overdischarge threshold */
#define AEM15820_IRQ_VOVDIS       BIT(1)
/** Storage element crossed the charge ready threshold */
#define AEM15820_IRQ_VCHRDY       BIT(2)
/** Storage element crossed the overcharge threshold */
#define AEM15820_IRQ_VOVCH        BIT(3)
/** Source crossed the source low threshold */
#define AEM15820_IRQ_SRCLOW       BIT(4)
/** Temperature crossed a charge protection threshold */
#define AEM15820_IRQ_TEMPCH       BIT(5)
/** Temperature crossed a discharge protection threshold */
#define AEM15820_IRQ_TEMPDIS      BIT(6)
/** 5V charger connection state changed */
#define AEM15820_IRQ_CHG5VCONN    BIT(7)
/** MPPT evaluation started */
#define AEM15820_IRQ_SRCMPPTSTART BIT(8)
/** MPPT evaluation completed */
#define AEM15820_IRQ_SRCMPPTDONE  BIT(9)
/** Storage element voltage measurement completed */
#define AEM15820_IRQ_STODONE      BIT(12)
/** Temperature measurement completed */
#define AEM15820_IRQ_TEMPDONE     BIT(13)
/** APM window (or accumulation) completed */
#define AEM15820_IRQ_APMDONE      BIT(14)
/** APM error */
#define AEM15820_IRQ_APMERR       BIT(15)
/** @} */

struct aem15820_irq_callback;

/**
 * @brief AEM15820 IRQ event handler
 *
 * Runs from the system workqueue, so it may call the register access functions.
 *
 * @param dev AEM15820 core device
 * @param cb Callback structure that was registered
 * @param events Set of @a AEM15820_IRQ_* events that triggered the IRQ line,
 *               already filtered by @a aem15820_irq_callback.events.
 */
typedef void (*aem15820_irq_handler_t)(const struct device *dev, struct aem15820_irq_callback *cb,
				       uint16_t events);

/** AEM15820 IRQ event callback */
struct aem15820_irq_callback {
	/** Internal list node */
	sys_snode_t node;
	/** Function to run on event */
	aem15820_irq_handler_t handler;
	/** Set of @a AEM15820_IRQ_* events to run the handler for */
	uint16_t events;
};

/**
 * @brief Read a single register
 *
 * @param dev AEM15820 core device
 * @param reg Register address
 * @param val Register value output
 *
 * @retval 0 on success
 * @retval -errno on bus error
 */
int aem15820_reg_read(const struct device *dev, uint8_t reg, uint8_t *val);

/**
 * @brief Read consecutive registers
 *
 * @param dev AEM15820 core device
 * @param reg First register address
 * @param buf Output buffer
 * @param len Number of registers to read
 *
 * @retval 0 on success
 * @retval -errno on bus error
 */
int aem15820_reg_read_burst(const struct device *dev, uint8_t reg, void *buf, size_t len);

/**
 * @brief Write a single register
 *
 * @note Writes to configuration registers only take effect after @ref aem15820_config_apply.
 *
 * @param dev AEM15820 core device
 * @param reg Register address
 * @param val Value to write
 *
 * @retval 0 on success
 * @retval -errno on bus error
 */
int aem15820_reg_write(const struct device *dev, uint8_t reg, uint8_t val);

/**
 * @brief Write consecutive registers
 *
 * @param dev AEM15820 core device
 * @param reg First register address
 * @param buf Values to write
 * @param len Number of registers to write
 *
 * @retval 0 on success
 * @retval -EINVAL if @a len is too large
 * @retval -errno on bus error
 */
int aem15820_reg_write_burst(const struct device *dev, uint8_t reg, const void *buf, size_t len);

/**
 * @brief Read-modify-write a register
 *
 * @param dev AEM15820 core device
 * @param reg Register address
 * @param mask Bits to modify
 * @param val New value of the bits in @a mask
 *
 * @retval 0 on success
 * @retval -errno on bus error
 */
int aem15820_reg_update(const struct device *dev, uint8_t reg, uint8_t mask, uint8_t val);

/**
 * @brief Apply the configuration registers to the device
 *
 * Writes CTRL.UPDATE and waits for the register synchronisation to complete. After the first
 * call the device is configured from the I2C registers instead of the configuration pins. All
 * configuration registers (SRCREGU0 to APMACC) must hold valid values before calling this, as
 * some reset values are reserved.
 *
 * The device returns to pin configuration (with all registers at their reset values) whenever
 * it passes through RESET STATE, signalled by @a AEM15820_IRQ_I2CRDY.
 *
 * @param dev AEM15820 core device
 *
 * @retval 0 on success
 * @retval -ETIMEDOUT if the synchronisation did not complete
 * @retval -errno on bus error
 */
int aem15820_config_apply(const struct device *dev);

/**
 * @brief Enable or disable IRQ line events
 *
 * Event enables are tracked per bit, not reference counted. Each event should only be managed
 * by a single child driver.
 *
 * Event enables are only applied by the device on @ref aem15820_config_apply. While the device
 * is configured from the pins, the IRQENx registers accept writes but events other than I2CRDY
 * do not assert the IRQ line until the configuration is first applied. Once the configuration
 * has been applied, this function re-applies it after updating the enables. Enables are
 * restored automatically after the device passes through RESET STATE.
 *
 * @param dev AEM15820 core device
 * @param events Set of @a AEM15820_IRQ_* events to modify
 * @param enable True to enable the events, false to disable them
 *
 * @retval 0 on success
 * @retval -errno on bus error
 */
int aem15820_irq_enable(const struct device *dev, uint16_t events, bool enable);

/**
 * @brief Register an IRQ event callback
 *
 * @param dev AEM15820 core device
 * @param cb Callback to register, must remain valid until removed
 *
 * @retval 0 on success
 * @retval -EINVAL if the callback has no handler
 * @retval -ENOTSUP if no IRQ GPIO is configured
 */
int aem15820_irq_callback_add(const struct device *dev, struct aem15820_irq_callback *cb);

/**
 * @brief Remove an IRQ event callback
 *
 * @param dev AEM15820 core device
 * @param cb Callback to remove
 *
 * @retval 0 on success
 * @retval -EINVAL if the callback was not registered
 */
int aem15820_irq_callback_remove(const struct device *dev, struct aem15820_irq_callback *cb);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_MFD_AEM15820_H_ */
