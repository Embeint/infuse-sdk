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
#include <infuse/validation/imu.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static K_SEM_DEFINE(task_complete, 0, INT_MAX);
static atomic_t validators_registered;
static atomic_t validators_passed;
static atomic_t validators_failed;
static atomic_t validators_complete;

#if defined(CONFIG_INFUSE_IMU_BMI270)
#define IMU_COMPAT bosch_bmi270

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
#endif

int main(void)
{
	VALIDATION_REPORT_INFO("SYS", "Starting");
	for (;;) {
		k_sem_take(&task_complete, K_FOREVER);
		if (validators_registered == validators_complete) {
			break;
		}
	}
	VALIDATION_REPORT_INFO("SYS", "Complete");
	k_sleep(K_FOREVER);
	return 0;
}
