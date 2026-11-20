/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * Register definitions from ICM-20689 datasheet (DS-000143 v1.1)
 */

#ifndef INFUSE_SDK_DRIVERS_IMU_ICM20689_H_
#define INFUSE_SDK_DRIVERS_IMU_ICM20689_H_

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ICM20689_REG_SELF_TEST_X_GYRO  0x00
#define ICM20689_REG_SELF_TEST_X_ACCEL 0x0D

#define ICM20689_REG_SMPLRT_DIV 0x19

#define ICM20689_REG_CONFIG         0x1A
#define ICM20689_CONFIG_FIFO_MODE   BIT(6)
#define ICM20689_CONFIG_DLPF_CFG(x) ((x) & 0x07)

#define ICM20689_REG_GYRO_CONFIG       0x1B
#define ICM20689_GYRO_CONFIG_SELF_TEST (BIT(7) | BIT(6) | BIT(5))
#define ICM20689_GYRO_CONFIG_250DPS    (0 << 3)
#define ICM20689_GYRO_CONFIG_500DPS    (1 << 3)
#define ICM20689_GYRO_CONFIG_1000DPS   (2 << 3)
#define ICM20689_GYRO_CONFIG_2000DPS   (3 << 3)

#define ICM20689_REG_ACCEL_CONFIG       0x1C
#define ICM20689_ACCEL_CONFIG_SELF_TEST (BIT(7) | BIT(6) | BIT(5))
#define ICM20689_ACCEL_CONFIG_2G        (0 << 3)
#define ICM20689_ACCEL_CONFIG_4G        (1 << 3)
#define ICM20689_ACCEL_CONFIG_8G        (2 << 3)
#define ICM20689_ACCEL_CONFIG_16G       (3 << 3)

#define ICM20689_REG_ACCEL_CONFIG2           0x1D
/* Not documented in DS-000143, the FIFO is limited to 512 bytes unless set */
#define ICM20689_ACCEL_CONFIG2_FIFO_SIZE_4K  (BIT(7) | BIT(6))
#define ICM20689_ACCEL_CONFIG2_DEC2_CFG(x)   (((x) & 0x03) << 4)
#define ICM20689_ACCEL_CONFIG2_A_DLPF_CFG(x) ((x) & 0x07)

#define ICM20689_REG_FIFO_EN   0x23
#define ICM20689_FIFO_EN_GYRO  (BIT(6) | BIT(5) | BIT(4))
#define ICM20689_FIFO_EN_ACCEL BIT(3)

#define ICM20689_REG_INT_PIN_CFG        0x37
#define ICM20689_INT_PIN_CFG_LEVEL_LOW  BIT(7)
#define ICM20689_INT_PIN_CFG_OPEN_DRAIN BIT(6)

#define ICM20689_REG_INT_ENABLE        0x38
#define ICM20689_INT_ENABLE_FIFO_OFLOW BIT(4)

#define ICM20689_REG_INT_STATUS        0x3A
#define ICM20689_INT_STATUS_FIFO_OFLOW BIT(4)

#define ICM20689_REG_ACCEL_XOUT_H 0x3B

#define ICM20689_REG_USER_CTRL        0x6A
#define ICM20689_USER_CTRL_FIFO_EN    BIT(6)
#define ICM20689_USER_CTRL_I2C_IF_DIS BIT(4)
#define ICM20689_USER_CTRL_FIFO_RST   BIT(2)

#define ICM20689_REG_PWR_MGMT_1          0x6B
#define ICM20689_PWR_MGMT_1_DEVICE_RESET BIT(7)
#define ICM20689_PWR_MGMT_1_SLEEP        BIT(6)
#define ICM20689_PWR_MGMT_1_ACCEL_CYCLE  BIT(5)
#define ICM20689_PWR_MGMT_1_TEMP_DIS     BIT(3)
#define ICM20689_PWR_MGMT_1_CLKSEL_AUTO  0x01

#define ICM20689_REG_PWR_MGMT_2        0x6C
#define ICM20689_PWR_MGMT_2_FIFO_LP_EN BIT(7)
#define ICM20689_PWR_MGMT_2_STBY_ACCEL (BIT(5) | BIT(4) | BIT(3))
#define ICM20689_PWR_MGMT_2_STBY_GYRO  (BIT(2) | BIT(1) | BIT(0))

#define ICM20689_REG_FIFO_COUNTH 0x72
#define ICM20689_FIFO_COUNT_MASK 0x1FFF
#define ICM20689_REG_FIFO_R_W    0x74

#define ICM20689_REG_WHO_AM_I 0x75
#define ICM20689_WHO_AM_I     0x98

/* Hardware FIFO size in bytes */
#define ICM20689_FIFO_SIZE 4096

/* Internal sample rate when the digital low pass filters are in use */
#define ICM20689_INTERNAL_RATE_HZ 1000

/* Start-up time for register read/write from sleep mode */
#define ICM20689_REG_STARTUP_MS 5

/* Sensor start-up times from sleep mode */
#define ICM20689_ACCEL_STARTUP_MS 20
#define ICM20689_GYRO_STARTUP_MS  35

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_DRIVERS_IMU_ICM20689_H_ */
