/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_DRIVERS_IMU_IIS328DQ_IIS328DQ_H_
#define INFUSE_SDK_DRIVERS_IMU_IIS328DQ_IIS328DQ_H_

#include <stdint.h>

#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>

enum {
	IIS328DQ_REG_WHO_AM_I = 0x0F,
	IIS328DQ_REG_CTRL1 = 0x20,
	IIS328DQ_REG_CTRL2 = 0x21,
	IIS328DQ_REG_CTRL3 = 0x22,
	IIS328DQ_REG_CTRL4 = 0x23,
	IIS328DQ_REG_CTRL5 = 0x24,
	IIS328DQ_REG_STATUS = 0x27,
	IIS328DQ_REG_OUT_X_L = 0x28,
};

#define IIS328DQ_WHO_AM_I 0x32

/* CTRL_REG1 */
#define IIS328DQ_CTRL1_XYZ_EN        0x07
#define IIS328DQ_CTRL1_DR_50HZ       (0 << 3)
#define IIS328DQ_CTRL1_DR_100HZ      (1 << 3)
#define IIS328DQ_CTRL1_DR_400HZ      (2 << 3)
#define IIS328DQ_CTRL1_DR_1000HZ     (3 << 3)
#define IIS328DQ_CTRL1_PM_POWER_DOWN (0 << 5)
#define IIS328DQ_CTRL1_PM_NORMAL     (1 << 5)
#define IIS328DQ_CTRL1_PM_LP_0HZ5    (2 << 5)
#define IIS328DQ_CTRL1_PM_LP_1HZ     (3 << 5)
#define IIS328DQ_CTRL1_PM_LP_2HZ     (4 << 5)
#define IIS328DQ_CTRL1_PM_LP_5HZ     (5 << 5)
#define IIS328DQ_CTRL1_PM_LP_10HZ    (6 << 5)

/* CTRL_REG2 */
#define IIS328DQ_CTRL2_BOOT BIT(7)

/* CTRL_REG3 */
#define IIS328DQ_CTRL3_I1_CFG_DRDY (2 << 0)
#define IIS328DQ_CTRL3_I2_CFG_DRDY (2 << 3)
#define IIS328DQ_CTRL3_PP_OD       BIT(6)
#define IIS328DQ_CTRL3_IHL         BIT(7)

/* CTRL_REG4 */
#define IIS328DQ_CTRL4_ST      BIT(1)
#define IIS328DQ_CTRL4_ST_SIGN BIT(3)
#define IIS328DQ_CTRL4_FS_2G   (0 << 4)
#define IIS328DQ_CTRL4_FS_4G   (1 << 4)
#define IIS328DQ_CTRL4_FS_8G   (3 << 4)
#define IIS328DQ_CTRL4_BDU     BIT(7)

/* STATUS_REG */
#define IIS328DQ_STATUS_ZYXDA BIT(3)
#define IIS328DQ_STATUS_ZYXOR BIT(7)

/** STATUS_REG followed by the output registers, read in a single burst */
struct iis328dq_status_frame {
	uint8_t status;
	int16_t x;
	int16_t y;
	int16_t z;
} __packed;

/* Time from power-up until registers are accessible */
#define IIS328DQ_BOOT_DELAY_MS          10
/* Maximum time to wait for the BOOT bit to clear (no limit specified in the datasheet) */
#define IIS328DQ_BOOT_RELOAD_TIMEOUT_MS 150

/* Self-test output change limits (12-bit LSB, +-2G, positive sign), DS10757 Table 2 */
#define IIS328DQ_SELF_TEST_X_MIN -1100
#define IIS328DQ_SELF_TEST_X_MAX -500
#define IIS328DQ_SELF_TEST_Y_MIN 500
#define IIS328DQ_SELF_TEST_Y_MAX 1100
#define IIS328DQ_SELF_TEST_Z_MIN 400
#define IIS328DQ_SELF_TEST_Z_MAX 800

#if CONFIG_IIS328DQ_BUS_SPI
extern const struct iis328dq_bus_io iis328dq_bus_io_spi;
#endif

#if CONFIG_IIS328DQ_BUS_I2C
extern const struct iis328dq_bus_io iis328dq_bus_io_i2c;
#endif

union iis328dq_bus {
#if CONFIG_IIS328DQ_BUS_SPI
	struct spi_dt_spec spi;
#endif
#if CONFIG_IIS328DQ_BUS_I2C
	struct i2c_dt_spec i2c;
#endif
};

typedef int (*iis328dq_bus_check_fn)(const union iis328dq_bus *bus);
typedef int (*iis328dq_reg_read_fn)(const union iis328dq_bus *bus, uint8_t start, uint8_t *data,
				    uint16_t len);
typedef int (*iis328dq_reg_write_fn)(const union iis328dq_bus *bus, uint8_t start,
				     const uint8_t *data, uint16_t len);

struct iis328dq_bus_io {
	iis328dq_bus_check_fn check;
	iis328dq_reg_read_fn read;
	iis328dq_reg_write_fn write;
};

#endif /* INFUSE_SDK_DRIVERS_IMU_IIS328DQ_IIS328DQ_H_ */
