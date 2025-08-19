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
#include <zephyr/sys/sys_heap.h>

#include <infuse/security.h>
#include <infuse/validation/bluetooth.h>
#include <infuse/validation/button.h>
#include <infuse/validation/core.h>
#include <infuse/validation/disk.h>
#include <infuse/validation/env.h>
#include <infuse/validation/imu.h>
#include <infuse/validation/leds.h>
#include <infuse/validation/lora.h>
#include <infuse/validation/pwr.h>
#include <infuse/validation/flash.h>
#include <infuse/validation/gnss.h>
#include <infuse/validation/nrf_modem.h>
#include <infuse/validation/wifi.h>

#ifdef CONFIG_NRF_MODEM_LIB
#include <modem/nrf_modem_lib.h>
#endif

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static K_SEM_DEFINE(task_complete, 0, INT_MAX);
static atomic_t validators_registered;
static atomic_t validators_passed;
static atomic_t validators_failed;
static atomic_t validators_complete;

#if DT_NODE_EXISTS(DT_ALIAS(imu0))
static int imu_validator(void *a, void *b, void *c)
{
	atomic_inc(&validators_registered);
	if (infuse_validation_imu(DEVICE_DT_GET(DT_ALIAS(imu0)),
				  VALIDATION_IMU_SELF_TEST | VALIDATION_IMU_DRIVER) == 0) {
		atomic_inc(&validators_passed);
	} else {
		atomic_inc(&validators_failed);
	}
	atomic_inc(&validators_complete);
	k_sem_give(&task_complete);
	return 0;
}

K_THREAD_DEFINE(imu_thread, 2048, imu_validator, NULL, NULL, NULL, 5, 0, 0);
#endif /* DT_NODE_EXISTS(DT_ALIAS(imu0)) */

#if DT_NODE_EXISTS(DT_ALIAS(environmental0))
static int env_validator(void *a, void *b, void *c)
{
	atomic_inc(&validators_registered);
	if (infuse_validation_env(DEVICE_DT_GET(DT_ALIAS(environmental0)), VALIDATION_ENV_DRIVER) ==
	    0) {
		atomic_inc(&validators_passed);
	} else {
		atomic_inc(&validators_failed);
	}
	atomic_inc(&validators_complete);
	k_sem_give(&task_complete);
	return 0;
}

K_THREAD_DEFINE(env_thread, 2048, env_validator, NULL, NULL, NULL, 5, 0, 0);
#endif /* DT_NODE_EXISTS(DT_ALIAS(environmental0)) */

#if DT_NODE_EXISTS(DT_ALIAS(fuel_gauge0))
static int pwr_validator(void *a, void *b, void *c)
{
	atomic_inc(&validators_registered);
	if (infuse_validation_pwr(DEVICE_DT_GET(DT_ALIAS(fuel_gauge0)),
				  VALIDATION_PWR_BATTERY_VOLTAGE | VALIDATION_PWR_BATTERY_CURRENT |
					  VALIDATION_PWR_BATTERY_SOC |
					  VALIDATION_PWR_BATTERY_TEMPERATURE) == 0) {
		atomic_inc(&validators_passed);
	} else {
		atomic_inc(&validators_failed);
	}
	atomic_inc(&validators_complete);
	k_sem_give(&task_complete);
	return 0;
}

K_THREAD_DEFINE(pwr_thread, 2048, pwr_validator, NULL, NULL, NULL, 5, 0, 0);

#endif /* DT_NODE_EXISTS(DT_ALIAS(fuel_gauge0)) */

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
#endif /* FLASH_COMPAT */

#if DT_NODE_EXISTS(DT_ALIAS(gnss))
static int gnss_validator(void *a, void *b, void *c)
{
	atomic_inc(&validators_registered);
	if (infuse_validation_gnss(DEVICE_DT_GET(DT_ALIAS(gnss)), VALIDATION_GNSS_POWER_UP) == 0) {
		atomic_inc(&validators_passed);
	} else {
		atomic_inc(&validators_failed);
	}
	atomic_inc(&validators_complete);
	k_sem_give(&task_complete);
	return 0;
}

K_THREAD_DEFINE(gnss_thread, 2048, gnss_validator, NULL, NULL, NULL, 5, 0, 0);
#endif /* DT_NODE_EXISTS(DT_ALIAS(gnss)) */

#ifdef CONFIG_DISK_DRIVER_SDMMC
static int disk_validator(void *a, void *b, void *c)
{
#ifdef CONFIG_SDMMC_STM32
	const char *const disk = DT_PROP(DT_COMPAT_GET_ANY_STATUS_OKAY(st_stm32_sdmmc), disk_name);
#else
	const char *const disk =
		DT_PROP(DT_COMPAT_GET_ANY_STATUS_OKAY(zephyr_sdmmc_disk), disk_name);
#endif

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
					VALIDATION_NRF_MODEM_SIM_CARD |
					VALIDATION_NRF_MODEM_LTE_SCAN) == 0) {
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

#if CONFIG_LORA
static void lora_validation_run(const struct device *dev)
{
	if (infuse_validation_lora(dev, VALIDATION_LORA_TX | VALIDATION_LORA_CAD) == 0) {
		atomic_inc(&validators_passed);
	} else {
		atomic_inc(&validators_failed);
	}
	atomic_inc(&validators_complete);
	k_sem_give(&task_complete);
}

static int lora_validator(void *a, void *b, void *c)
{
	atomic_inc(&validators_registered);
#if DT_NODE_EXISTS(DT_ALIAS(lora1))
	atomic_inc(&validators_registered);
	lora_validation_run(DEVICE_DT_GET(DT_ALIAS(lora1)));
#endif
	lora_validation_run(DEVICE_DT_GET(DT_ALIAS(lora0)));
	return 0;
}

K_THREAD_DEFINE(lora_thread, 2048, lora_validator, NULL, NULL, NULL, 5, 0, 0);
#endif /* CONFIG_LORA */

#ifdef CONFIG_WIFI

static int wifi_validator(void *a, void *b, void *c)
{
	struct net_if *iface;

	atomic_inc(&validators_registered);

	iface = net_if_get_first_wifi();
	if (iface == NULL) {
		VALIDATION_REPORT_ERROR("SYS", "Failed to retrieve WiFi interface");
		atomic_inc(&validators_failed);
		goto end;
	}

	if (infuse_validation_wifi(iface, VALIDATION_WIFI_POWER_UP | VALIDATION_WIFI_SSID_SCAN) ==
	    0) {
		atomic_inc(&validators_passed);
	} else {
		atomic_inc(&validators_failed);
	}
end:
	atomic_inc(&validators_complete);
	k_sem_give(&task_complete);
	return 0;
}

K_THREAD_DEFINE(wifi_thread, 6144, wifi_validator, NULL, NULL, NULL, 5, 0, 0);
#endif /* CONFIG_WIFI */

#ifdef CONFIG_INFUSE_VALIDATION_BUTTON_REQUIRE_MANUAL
static int button_validator(void *a, void *b, void *c)
{
	static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

	atomic_inc(&validators_registered);

	if (infuse_validation_button(&button, VALIDATION_BUTTON_MODE_BOTH) == 0) {
		atomic_inc(&validators_passed);
	} else {
		atomic_inc(&validators_failed);
	}
	atomic_inc(&validators_complete);
	k_sem_give(&task_complete);
	return 0;
}

K_THREAD_DEFINE(button_thread, 512, button_validator, NULL, NULL, NULL, 5, 0, 0);
#endif /* CONFIG_INFUSE_VALIDATION_BUTTON_REQUIRE_MANUAL */

#ifdef CONFIG_INFUSE_VALIDATION_LEDS
#define GET_GPIO_SPEC(gpio_spec) GPIO_DT_SPEC_GET(gpio_spec, gpios)
static int leds_validator(void *a, void *b, void *c)
{
	/* clang-format off */
	const struct gpio_dt_spec leds[] = {
		DT_FOREACH_CHILD_SEP(DT_PATH(leds), GET_GPIO_SPEC, (,))};
	/* clang-format on */

	atomic_inc(&validators_registered);

	if (infuse_validation_leds(leds, ARRAY_SIZE(leds), VALIDATION_LEDS_OBSERVE_ONLY) == 0) {
		atomic_inc(&validators_passed);
	} else {
		atomic_inc(&validators_failed);
	}
	atomic_inc(&validators_complete);
	k_sem_give(&task_complete);
	return 0;
}

K_THREAD_DEFINE(leds_thread, 1024, leds_validator, NULL, NULL, NULL, 5, 0, 0);
#endif /* CONFIG_INFUSE_VALIDATION_LED */

#if DT_NODE_EXISTS(DT_COMPAT_GET_ANY_STATUS_OKAY(embeint_epacket_bt_adv))
static int bt_validator(void *a, void *b, void *c)
{
	atomic_inc(&validators_registered);
	if (infuse_validation_bluetooth(VALIDATION_BLUETOOTH_ADV_TX) == 0) {
		atomic_inc(&validators_passed);
	} else {
		atomic_inc(&validators_failed);
	}
	atomic_inc(&validators_complete);
	k_sem_give(&task_complete);
	return 0;
}

K_THREAD_DEFINE(bt_thread, 2048, bt_validator, NULL, NULL, NULL, 5, 0, 0);
#endif /* DT_NODE_EXISTS(DT_COMPAT_GET_ANY_STATUS_OKAY(embeint_epacket_bt_adv)) */

static int validation_init(void)
{
	int rc;

#if defined(CONFIG_NRF_MODEM_LIB) && !defined(CONFIG_NRF_MODEM_LIB_NET_IF_AUTO_START)
	/* Some crypto functionality depends on the modem being initialised */
	VALIDATION_REPORT_INFO("SYS", "Initialising nRF modem library");
	rc = nrf_modem_lib_init();
	if (rc < 0) {
		VALIDATION_REPORT_ERROR("SYS", "Failed to initialise nRF modem library (%d)", rc);
	}
#endif /* defined(CONFIG_NRF_MODEM_LIB) && !defined(CONFIG_NRF_MODEM_LIB_NET_IF_AUTO_START) */

	rc = infuse_security_init();
	if (rc < 0) {
		VALIDATION_REPORT_ERROR("SYS", "Security init failed");
	}
	return 0;
}

SYS_INIT(validation_init, APPLICATION, 99);

int main(void)
{
	VALIDATION_REPORT_INFO("SYS", "Starting");
	for (;;) {
		k_sem_take(&task_complete, K_FOREVER);
		if (validators_registered == validators_complete) {
			break;
		}
	}

#ifdef CONFIG_SYS_HEAP_RUNTIME_STATS
	struct sys_memory_stats heap_stats;
	struct sys_heap **heaps;
	int num_heaps;
	int rc;

	num_heaps = sys_heap_array_get(&heaps);
	for (int i = 0; i < num_heaps; i++) {
		rc = sys_heap_runtime_stats_get(heaps[i], &heap_stats);
		if ((rc != 0) || (heaps[i]->init_bytes == 0)) {
			continue;
		}
		VALIDATION_REPORT_INFO("SYS", "Heap %p= Current %6d Max %6d Size %6d", heaps[i],
				       heap_stats.allocated_bytes, heap_stats.max_allocated_bytes,
				       heaps[i]->init_bytes);
	}

#endif /* CONFIG_SYS_HEAP_RUNTIME_STATS */

	(void)validators_failed;
	VALIDATION_REPORT_INFO("SYS", "Complete with %d/%d passed", (int32_t)validators_passed,
			       (int32_t)validators_registered);
	k_sleep(K_FOREVER);
	return 0;
}
