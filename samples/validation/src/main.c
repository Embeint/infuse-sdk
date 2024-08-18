/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <infuse/validation/core.h>
#include <infuse/validation/disk.h>
#include <infuse/validation/env.h>
#include <infuse/validation/imu.h>
#include <infuse/validation/pwr.h>
#include <infuse/validation/flash.h>
#include <infuse/validation/gnss.h>
#include <infuse/validation/nrf_modem.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static K_SEM_DEFINE(task_complete, 0, INT_MAX);
static atomic_t validators_registered;
static atomic_t validators_passed;
static atomic_t validators_failed;
static atomic_t validators_complete;

#if defined(CONFIG_INFUSE_IMU_BMI270)
#define IMU_COMPAT bosch_bmi270
#elif defined(CONFIG_INFUSE_IMU_LSM6DSV)
#define IMU_COMPAT st_lsm6dsv16x
#endif

#ifdef IMU_COMPAT
static int imu_validator(void *a, void *b, void *c)
{
	atomic_inc(&validators_registered);
	if (infuse_validation_imu(DEVICE_DT_GET_ONE(IMU_COMPAT), VALIDATION_IMU_DRIVER) == 0) {
		atomic_inc(&validators_passed);
	} else {
		atomic_inc(&validators_failed);
	}
	atomic_inc(&validators_complete);
	k_sem_give(&task_complete);
	return 0;
}

K_THREAD_DEFINE(imu_thread, 2048, imu_validator, NULL, NULL, NULL, 5, 0, 0);
#endif /* IMU_COMPAT */

#if defined(CONFIG_BME280)
#define ENV_COMPAT bosch_bme280
#endif

#ifdef ENV_COMPAT
static int env_validator(void *a, void *b, void *c)
{
	atomic_inc(&validators_registered);
	if (infuse_validation_env(DEVICE_DT_GET_ONE(ENV_COMPAT), VALIDATION_ENV_DRIVER) == 0) {
		atomic_inc(&validators_passed);
	} else {
		atomic_inc(&validators_failed);
	}
	atomic_inc(&validators_complete);
	k_sem_give(&task_complete);
	return 0;
}

K_THREAD_DEFINE(env_thread, 2048, env_validator, NULL, NULL, NULL, 5, 0, 0);
#endif /* ENV_COMPAT */

#if DT_NODE_EXISTS(DT_ALIAS(battery0)) || DT_NODE_EXISTS(DT_ALIAS(charger0))

static int pwr_validator(void *a, void *b, void *c)
{
	atomic_inc(&validators_registered);
	if (infuse_validation_pwr(DEVICE_DT_GET_OR_NULL(DT_ALIAS(battery0)),
				  DEVICE_DT_GET_OR_NULL(DT_ALIAS(charger0)),
				  VALIDATION_PWR_DRIVER) == 0) {
		atomic_inc(&validators_passed);
	} else {
		atomic_inc(&validators_failed);
	}
	atomic_inc(&validators_complete);
	k_sem_give(&task_complete);
	return 0;
}

K_THREAD_DEFINE(pwr_thread, 2048, pwr_validator, NULL, NULL, NULL, 5, 0, 0);

#endif /* DT_NODE_EXISTS(DT_ALIAS(battery0)) || DT_NODE_EXISTS(DT_ALIAS(charger0)) */

#if defined(CONFIG_SPI_NOR)
#define FLASH_COMPAT jedec_spi_nor
#endif

#ifdef FLASH_COMPAT
static int flash_validator(void *a, void *b, void *c)
{
	atomic_inc(&validators_registered);
	if (infuse_validation_flash(DEVICE_DT_GET_ONE(FLASH_COMPAT), VALIDATION_FLASH_DRIVER) ==
	    0) {
		atomic_inc(&validators_passed);
	} else {
		atomic_inc(&validators_failed);
	}
	atomic_inc(&validators_complete);
	k_sem_give(&task_complete);
	return 0;
}

K_THREAD_DEFINE(flash_thread, 2048, flash_validator, NULL, NULL, NULL, 5, 0, 0);
#endif /* IMU_COMPAT */

#if defined(CONFIG_GNSS_U_BLOX_M10_I2C)
#define GNSS_COMPAT u_blox_m10_i2c
#endif

#ifdef GNSS_COMPAT
static int gnss_validator(void *a, void *b, void *c)
{
	atomic_inc(&validators_registered);
	if (infuse_validation_gnss(DEVICE_DT_GET_ONE(GNSS_COMPAT), VALIDATION_GNSS_POWER_UP) == 0) {
		atomic_inc(&validators_passed);
	} else {
		atomic_inc(&validators_failed);
	}
	atomic_inc(&validators_complete);
	k_sem_give(&task_complete);
	return 0;
}

K_THREAD_DEFINE(gnss_thread, 2048, gnss_validator, NULL, NULL, NULL, 5, 0, 0);
#endif /* GNSS_COMPAT */

#ifdef CONFIG_DISK_DRIVER_SDMMC

static int disk_validator(void *a, void *b, void *c)
{
	const char *disk = DT_PROP(DT_COMPAT_GET_ANY_STATUS_OKAY(zephyr_sdmmc_disk), disk_name);

	atomic_inc(&validators_registered);
	if (infuse_validation_disk(disk, VALIDATION_DISK_DRIVER) == 0) {
		atomic_inc(&validators_passed);
	} else {
		atomic_inc(&validators_failed);
	}
	atomic_inc(&validators_complete);
	k_sem_give(&task_complete);
	return 0;
}

K_THREAD_DEFINE(disk_thread, 2048, disk_validator, NULL, NULL, NULL, 5, 0, 0);
#endif /* CONFIG_DISK_DRIVER_SDMMC */

#if CONFIG_NRF_MODEM_LIB

static int nrf_modem_validator(void *a, void *b, void *c)
{
	atomic_inc(&validators_registered);
	if (infuse_validation_nrf_modem(VALIDATION_NRF_MODEM_FW_VERSION |
					VALIDATION_NRF_MODEM_SIM_CARD) == 0) {
		atomic_inc(&validators_passed);
	} else {
		atomic_inc(&validators_failed);
	}
	atomic_inc(&validators_complete);
	k_sem_give(&task_complete);
	return 0;
}

K_THREAD_DEFINE(nrf_modem_thread, 2048, nrf_modem_validator, NULL, NULL, NULL, 5, 0, 0);
#endif /* CONFIG_NRF_MODEM_LIB */

int main(void)
{
	VALIDATION_REPORT_INFO("SYS", "Starting");
	for (;;) {
		k_sem_take(&task_complete, K_FOREVER);
		if (validators_registered == validators_complete) {
			break;
		}
	}
	(void)validators_failed;
	VALIDATION_REPORT_INFO("SYS", "Complete with %d/%d passed", (int32_t)validators_passed,
			       (int32_t)validators_registered);
	k_sleep(K_FOREVER);
	return 0;
}
