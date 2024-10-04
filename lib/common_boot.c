/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/init.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/hwinfo.h>

#include <infuse/version.h>
#include <infuse/identifiers.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/reboot.h>
#include <infuse/time/epoch.h>
#include <infuse/security.h>

#ifdef CONFIG_NRF_MODEM_LIB
#include <modem/nrf_modem_lib.h>
#endif

LOG_MODULE_REGISTER(infuse, CONFIG_INFUSE_COMMON_LOG_LEVEL);

IF_DISABLED(CONFIG_ZTEST, (static))
struct infuse_reboot_state reboot_state;

int infuse_common_boot_last_reboot(struct infuse_reboot_state *state)
{
	*state = reboot_state;
	return state->reason == INFUSE_REBOOT_UNKNOWN ? -ENOENT : 0;
}

static int infuse_common_boot(void)
{
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboot = {0};
	struct infuse_version v = application_version_get();
	int rc;
#ifdef CONFIG_INFUSE_SDK
	uint64_t device_id = infuse_device_id();
#else
	uint64_t device_id = 0;
#endif /* CONFIG_INFUSE_SDK */

#ifdef CONFIG_INFUSE_SECURITY
	if (infuse_security_init() < 0) {
		LOG_ERR("Failed to initialise security");
	}
#endif /* CONFIG_INFUSE_SECURITY */

#ifdef CONFIG_KV_STORE
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboot_fallback = {0};

	/* Get current reboot count */
	rc = kv_store_read_fallback(KV_KEY_REBOOTS, &reboot, sizeof(reboot), &reboot_fallback,
				    sizeof(reboot_fallback));
	if (rc == sizeof(reboot)) {
		/* Increment reboot counter */
		reboot.count += 1;
		(void)KV_STORE_WRITE(KV_KEY_REBOOTS, &reboot);
	}
#endif /* CONFIG_KV_STORE */

#ifdef CONFIG_USB_DEVICE_STACK
#ifndef CONFIG_USB_DEVICE_INITIALIZE_AT_BOOT
	rc = usb_enable(NULL);
	if (rc != 0) {
		LOG_ERR("USB enable error (%d)", rc);
	}
#endif /* CONFIG_USB_DEVICE_INITIALIZE_AT_BOOT */
#endif /* CONFIG_USB_DEVICE_STACK */
#ifdef CONFIG_BT
	rc = bt_enable(NULL);
	if (rc) {
		LOG_ERR("Failed to enable Bluetooth (%d)", rc);
	}
#endif /* CONFIG_BT */

	LOG_INF("\tVersion: %d.%d.%d+%08x", v.major, v.minor, v.revision, v.build_num);
	LOG_INF("\t Device: %016llx", device_id);
	LOG_INF("\t  Board: %s", CONFIG_BOARD);
#ifdef CONFIG_BT
	const char *bt_addr_le_str(const bt_addr_le_t *addr);
	bt_addr_le_t bt_addr[CONFIG_BT_ID_MAX];
	size_t bt_addr_cnt = ARRAY_SIZE(bt_addr);
	KV_KEY_TYPE(KV_KEY_BLUETOOTH_ADDR) bluetooth_addr = {0};

	bt_id_get(bt_addr, &bt_addr_cnt);
	LOG_INF("\tBT Addr: %s", bt_addr_le_str(&bt_addr[0]));

	/* Push address into KV store */
	memcpy(&bluetooth_addr, &bt_addr[0], sizeof(bluetooth_addr));
	(void)KV_STORE_WRITE(KV_KEY_BLUETOOTH_ADDR, &bluetooth_addr);
#endif /* CONFIG_BT */
#ifdef CONFIG_KV_STORE_KEY_LTE_SIM_UICC
	KV_STRUCT_KV_STRING_VAR(24) sim_uicc;

	if (KV_STORE_READ(KV_KEY_LTE_SIM_UICC, &sim_uicc) > 0) {
		LOG_INF("\t    SIM: %s", sim_uicc.value);
	}
#endif /* CONFIG_KV_STORE_KEY_LTE_SIM_UICC */
	LOG_INF("\tReboots: %d", reboot.count);
#ifdef CONFIG_INFUSE_REBOOT
	struct timeutil_sync_instant reference;
	uint32_t reset_cause = 0;

	/* Query any reboot state */
	rc = infuse_reboot_state_query(&reboot_state);
	if (rc != 0) {
		/* No stored state, so fallback to hardware flags only */
		(void)hwinfo_get_reset_cause(&reset_cause);
		(void)hwinfo_clear_reset_cause();
		reboot_state.hardware_reason = reset_cause;
		reboot_state.reason = INFUSE_REBOOT_UNKNOWN;
	}

	/* Print the reboot information/causes */
	LOG_INF("");
	LOG_INF("Reboot Information");
	LOG_INF("\tHardware: %08X", reboot_state.hardware_reason);
	if (rc == 0) {
		LOG_INF("\t   Cause: %d", reboot_state.reason);
		LOG_INF("\t  Uptime: %d", reboot_state.uptime);
		LOG_INF("\t  Thread: %s", reboot_state.thread_name);
		LOG_INF("\t PC/WDOG: %08X", reboot_state.param_1.program_counter);
		LOG_INF("\t LR/WDOG: %08X", reboot_state.param_2.link_register);

		/* Restore time knowledge (Assume reboot took 0 ms) */
		reference.local = 0;
		reference.ref = reboot_state.epoch_time;
		epoch_time_set_reference(TIME_SOURCE_RECOVERED | reboot_state.epoch_time_source,
					 &reference);
	} else {
		LOG_INF("\t   Cause: Unknown");
	}
#else
	reboot_state.reason = INFUSE_REBOOT_UNKNOWN;
#endif /* CONFIG_INFUSE_REBOOT */

#if defined(CONFIG_NRF_MODEM_LIB) && !defined(CONFIG_NRF_MODEM_LIB_NET_IF_AUTO_START)
	/* nRF modems are not low power until the library has been initialised */
	LOG_DBG("Initialising nRF modem library");
	rc = nrf_modem_lib_init();
	if (rc < 0) {
		LOG_ERR("Failed to initialise nRF modem library (%d)", rc);
	}
#endif /* defined(CONFIG_NRF_MODEM_LIB) && !defined(CONFIG_NRF_MODEM_LIB_NET_IF_AUTO_START) */

	(void)rc;
	return 0;
}

SYS_INIT(infuse_common_boot, APPLICATION, 10);
