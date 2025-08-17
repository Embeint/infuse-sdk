/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_DRIVERS_IMU_LIS2DW12_LIS2DW12_H_
#define INFUSE_SDK_DRIVERS_IMU_LIS2DW12_LIS2DW12_H_

#include <stdint.h>

#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>

enum {
	LIS2DW12_REG_OUT_T_L = 0x0D,
	LIS2DW12_REG_OUT_T_H = 0x0E,
	LIS2DW12_REG_WHO_AM_I = 0x0F,
	LIS2DW12_REG_CTRL1 = 0x20,
	LIS2DW12_REG_CTRL2 = 0x21,
	LIS2DW12_REG_CTRL3 = 0x22,
	LIS2DW12_REG_CTRL4_INT1_PAD = 0x23,
	LIS2DW12_REG_CTRL5_INT2_PAD = 0x24,
	LIS2DW12_REG_CTRL6 = 0x25,
	LIS2DW12_REG_OUT_T = 0x26,
	LIS2DW12_REG_STATUS = 0x27,
	LIS2DW12_REG_OUT_X_L = 0x28,
	LIS2DW12_REG_OUT_X_H = 0x29,
	LIS2DW12_REG_OUT_Y_L = 0x2A,
	LIS2DW12_REG_OUT_Y_H = 0x2B,
	LIS2DW12_REG_OUT_Z_L = 0x2C,
	LIS2DW12_REG_OUT_Z_H = 0x2D,
	LIS2DW12_REG_FIFO_CTRL = 0x2E,
	LIS2DW12_REG_FIFO_SAMPLES = 0x2F,
	LIS2DW12_REG_CTRL7 = 0x3F,
};

#define LIS2DW12_WHO_AM_I 0x44

#define LIS2DW_CTRL1_ODR_POWER_DOWN (0 << 4)
#define LIS2DW_CTRL1_ODR_12HZ5_1HZ6 (1 << 4)
#define LIS2DW_CTRL1_ODR_12HZ5      (2 << 4)
#define LIS2DW_CTRL1_ODR_25HZ       (3 << 4)
#define LIS2DW_CTRL1_ODR_50HZ       (4 << 4)
#define LIS2DW_CTRL1_ODR_100HZ      (5 << 4)
#define LIS2DW_CTRL1_ODR_200HZ      (6 << 4)
#define LIS2DW_CTRL1_ODR_400HZ      (7 << 4)
#define LIS2DW_CTRL1_ODR_800HZ      (8 << 4)
#define LIS2DW_CTRL1_ODR_1600HZ     (9 << 4)

#define LIS2DW_CTRL1_MODE_LOW_POWER        (0 << 2)
#define LIS2DW_CTRL1_MODE_HIGH_PERFORMANCE (1 << 2)
#define LIS2DW_CTRL1_MODE_ONE_SHOT         (2 << 2)

#define LIS2DW_CTRL1_MODE_LPM1 (0 << 0)
#define LIS2DW_CTRL1_MODE_LPM2 (1 << 0)
#define LIS2DW_CTRL1_MODE_LPM3 (2 << 0)
#define LIS2DW_CTRL1_MODE_LPM4 (3 << 0)

#define LIS2DW_CTRL2_SIM         BIT(0)
#define LIS2DW_CTRL2_I2C_DISABLE BIT(1)
#define LIS2DW_CTRL2_IF_ADD_INC  BIT(2)
#define LIS2DW_CTRL2_BDU         BIT(3)
#define LIS2DW_CTRL2_CS_PU_DISC  BIT(4)
#define LIS2DW_CTRL2_SOFT_RESET  BIT(6)
#define LIS2DW_CTRL2_BOOT        BIT(7)

#define LIS2DW_CTRL3_SLP_MODE_1         BIT(0)
#define LIS2DW_CTRL3_SLP_MODE_SEL       BIT(1)
#define LIS2DW_CTRL3_INT_ACTIVE_LOW     BIT(3)
#define LIS2DW_CTRL3_INT_ACTIVE_HIGH    0
#define LIS2DW_CTRL3_LIR                BIT(4)
#define LIS2DW_CTRL3_OPEN_DRAIN         BIT(5)
#define LIS2DW_CTRL3_PUSH_PULL          0
#define LIS2DW_CTRL3_SELF_TEST_NONE     (0 << 6)
#define LIS2DW_CTRL3_SELF_TEST_POSITIVE (1 << 6)
#define LIS2DW_CTRL3_SELF_TEST_NEGATIVE (2 << 6)

#define LIS2DW_CTRL4_INT1_DRDY       BIT(0)
#define LIS2DW_CTRL4_INT1_FTH        BIT(1)
#define LIS2DW_CTRL4_INT1_DIFF5      BIT(2)
#define LIS2DW_CTRL4_INT1_TAP        BIT(3)
#define LIS2DW_CTRL4_INT1_FF         BIT(4)
#define LIS2DW_CTRL4_INT1_WU         BIT(5)
#define LIS2DW_CTRL4_INT1_SINGLE_TAP BIT(6)
#define LIS2DW_CTRL4_INT1_6D         BIT(7)

#define LIS2DW_CTRL5_INT2_DRDY        BIT(0)
#define LIS2DW_CTRL5_INT2_FTH         BIT(1)
#define LIS2DW_CTRL5_INT2_DIFF5       BIT(2)
#define LIS2DW_CTRL5_INT2_OVR         BIT(3)
#define LIS2DW_CTRL5_INT2_DRDY_T      BIT(4)
#define LIS2DW_CTRL5_INT2_BOOT        BIT(5)
#define LIS2DW_CTRL5_INT2_SLEEP_CHG   BIT(6)
#define LIS2DW_CTRL5_INT2_SLEEP_STATE BIT(7)

#define LIS2DW_CTRL6_LOW_NOISE        BIT(2)
#define LIS2DW_CTRL6_FILTER_LOW_PASS  0
#define LIS2DW_CTRL6_FILTER_HIGH_PASS BIT(3)
#define LIS2DW_CTRL6_FS_2G            (0 << 4)
#define LIS2DW_CTRL6_FS_4G            (1 << 4)
#define LIS2DW_CTRL6_FS_8G            (2 << 4)
#define LIS2DW_CTRL6_FS_16G           (3 << 4)
#define LIS2DW_CTRL6_FILTER_BW_ODR2   (0 << 6)
#define LIS2DW_CTRL6_FILTER_BW_ODR4   (1 << 6)
#define LIS2DW_CTRL6_FILTER_BW_ODR10  (2 << 6)
#define LIS2DW_CTRL6_FILTER_BW_ODR20  (3 << 6)

#define LIS2DW_STATUS_DRDY        BIT(0)
#define LIS2DW_STATUS_FF_IA       BIT(1)
#define LIS2DW_STATUS_6D_IA       BIT(2)
#define LIS2DW_STATUS_SINGLE_TAP  BIT(3)
#define LIS2DW_STATUS_DOUBLE_TAP  BIT(4)
#define LIS2DW_STATUS_SLEEP_STATE BIT(5)
#define LIS2DW_STATUS_WU_IA       BIT(6)
#define LIS2DW_STATUS_FIFO_THS    BIT(7)

#define LIS2DW_FIFO_CTRL_THRESHOLD_MASK            0x1F
#define LIS2DW_FIFO_CTRL_MODE_BYPASS               (0 << 5)
#define LIS2DW_FIFO_CTRL_MODE_FIFO                 (1 << 5)
#define LIS2DW_FIFO_CTRL_MODE_CONTINUOUS_TO_FIFO   (3 << 5)
#define LIS2DW_FIFO_CTRL_MODE_BYPASS_TO_CONTINUOUS (4 << 5)
#define LIS2DW_FIFO_CTRL_MODE_CONTINUOUS           (6 << 5)

#define LIS2DW_FIFO_SAMPLES_COUNT_MASK 0x3F
#define LIS2DW_FIFO_SAMPLES_FIFO_OVR   BIT(6)
#define LIS2DW_FIFO_SAMPLES_FIFO_FTH   BIT(7)

#define LIS2DW_CTRL7_LPASS_ON6D        BIT(0)
#define LIS2DW_CTRL7_HP_REF_MODE       BIT(1)
#define LIS2DW_CTRL7_USR_OFF_W         BIT(2)
#define LIS2DW_CTRL7_USR_OFF_ON_WU     BIT(3)
#define LIS2DW_CTRL7_USR_OFF_ON_OUT    BIT(4)
#define LIS2DW_CTRL7_INTERRUPTS_ENABLE BIT(5)
#define LIS2DW_CTRL7_INT2_ON_INT1      BIT(6)
#define LIS2DW_CTRL7_DRDY_PULSED       BIT(7)

struct lis2dw12_fifo_frame {
	int16_t x;
	int16_t y;
	int16_t z;
} __packed;

#define LIS2DW12_POR_DELAY                30
#define LIS2DW12_FIFO_FRAME_SIZE          32
#define LIS2DW12_SELF_TEST_DEFLECTION_MIN 70
#define LIS2DW12_SELF_TEST_DEFLECTION_MAX 1500

#if CONFIG_LIS2DW12_BUS_SPI
extern const struct lis2dw12_bus_io lis2dw12_bus_io_spi;
#endif

#if CONFIG_LIS2DW12_BUS_I2C
extern const struct lis2dw12_bus_io lis2dw12_bus_io_i2c;
#endif

union lis2dw12_bus {
#if CONFIG_LIS2DW12_BUS_SPI
	struct spi_dt_spec spi;
#endif
#if CONFIG_LIS2DW12_BUS_I2C
	struct i2c_dt_spec i2c;
#endif
};

typedef int (*lis2dw12_bus_check_fn)(const union lis2dw12_bus *bus);
typedef int (*lis2dw12_bus_init_fn)(const union lis2dw12_bus *bus);
typedef int (*lis2dw12_reg_read_fn)(const union lis2dw12_bus *bus, uint8_t start, uint8_t *data,
				    uint16_t len);
typedef int (*lis2dw12_reg_write_fn)(const union lis2dw12_bus *bus, uint8_t start,
				     const uint8_t *data, uint16_t len);

struct lis2dw12_bus_io {
	lis2dw12_bus_check_fn check;
	lis2dw12_reg_read_fn read;
	lis2dw12_reg_write_fn write;
	lis2dw12_bus_init_fn init;
};

#endif /* INFUSE_SDK_DRIVERS_IMU_LIS2DW12_LIS2DW12_H_ */
