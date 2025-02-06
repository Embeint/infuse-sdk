/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/init.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/controller.h>
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
#include <infuse/drivers/watchdog.h>
#include <infuse/bluetooth/controller_manager.h>

#ifdef CONFIG_NRF_MODEM_LIB
#include <modem/nrf_modem_lib.h>
#endif

int infuse_board_public_bt_addr(bt_addr_le_t *addr);

LOG_MODULE_REGISTER(infuse, CONFIG_INFUSE_COMMON_LOG_LEVEL);

IF_DISABLED(CONFIG_ZTEST, (static))
struct infuse_reboot_state reboot_state;

int infuse_common_boot_last_reboot(struct infuse_reboot_state *state)
{
	*state = reboot_state;
	return state->reason == INFUSE_REBOOT_UNKNOWN ? -ENOENT : 0;
}

#ifdef CONFIG_TFM_PLATFORM_FAULT_INFO_QUERY

#ifdef CONFIG_CPU_CORTEX_M33
/* Secure Fault Status Register Definitions copied from core_cm33.h.
 * The file itself cannot be used due to overzealous #ifdef usage.
 * Fixed by: https://github.com/ARM-software/CMSIS_6/pull/218
 */
#define SAU_SFSR_LSERR_Pos 7U                          /*!< SAU SFSR: LSERR Position */
#define SAU_SFSR_LSERR_Msk (1UL << SAU_SFSR_LSERR_Pos) /*!< SAU SFSR: LSERR Mask */

#define SAU_SFSR_SFARVALID_Pos 6U                              /*!< SAU SFSR: SFARVALID Position */
#define SAU_SFSR_SFARVALID_Msk (1UL << SAU_SFSR_SFARVALID_Pos) /*!< SAU SFSR: SFARVALID Mask */

#define SAU_SFSR_LSPERR_Pos 5U                           /*!< SAU SFSR: LSPERR Position */
#define SAU_SFSR_LSPERR_Msk (1UL << SAU_SFSR_LSPERR_Pos) /*!< SAU SFSR: LSPERR Mask */

#define SAU_SFSR_INVTRAN_Pos 4U                            /*!< SAU SFSR: INVTRAN Position */
#define SAU_SFSR_INVTRAN_Msk (1UL << SAU_SFSR_INVTRAN_Pos) /*!< SAU SFSR: INVTRAN Mask */

#define SAU_SFSR_AUVIOL_Pos 3U                           /*!< SAU SFSR: AUVIOL Position */
#define SAU_SFSR_AUVIOL_Msk (1UL << SAU_SFSR_AUVIOL_Pos) /*!< SAU SFSR: AUVIOL Mask */

#define SAU_SFSR_INVER_Pos 2U                          /*!< SAU SFSR: INVER Position */
#define SAU_SFSR_INVER_Msk (1UL << SAU_SFSR_INVER_Pos) /*!< SAU SFSR: INVER Mask */

#define SAU_SFSR_INVIS_Pos 1U                          /*!< SAU SFSR: INVIS Position */
#define SAU_SFSR_INVIS_Msk (1UL << SAU_SFSR_INVIS_Pos) /*!< SAU SFSR: INVIS Mask */

#define SAU_SFSR_INVEP_Pos 0U                              /*!< SAU SFSR: INVEP Position */
#define SAU_SFSR_INVEP_Msk (1UL /*<< SAU_SFSR_INVEP_Pos*/) /*!< SAU SFSR: INVEP Mask */

#else
#error "Unsupported CPU"
#endif /* CONFIG_CPU_CORTEX_M33 */

#include <tfm_ioctl_api.h>

/* Ensure that the two fault frames match */
BUILD_ASSERT(sizeof(((struct arch_esf *)(NULL))->basic) ==
	     sizeof(((struct fault_exception_info_t *)(NULL))->EXC_FRAME_COPY));

#define ARCH_ESF_PC_IDX (offsetof(struct arch_esf, basic.pc) / sizeof(uint32_t))
#define ARCH_ESF_LR_IDX (offsetof(struct arch_esf, basic.lr) / sizeof(uint32_t))

static int secure_fault_info_read(void)
{
	uint8_t reason = K_ERR_ARM_SECURE_GENERIC;
	struct fault_exception_info_t secure_fault;
	enum tfm_platform_err_t err;
	char frame_ptr_str[9];
	uint32_t result = 0;

	err = tfm_platform_fault_info_read(&secure_fault, &result);
	if ((err != TFM_PLATFORM_ERR_SUCCESS) || (result != sizeof(secure_fault))) {
		/* No secure fault dump */
		return -ENODATA;
	}

	if ((secure_fault.SFSR & SAU_SFSR_INVEP_Msk) != 0) {
		reason = K_ERR_ARM_SECURE_ENTRY_POINT;
	} else if ((secure_fault.SFSR & SAU_SFSR_INVIS_Msk) != 0) {
		reason = K_ERR_ARM_SECURE_INTEGRITY_SIGNATURE;
	} else if ((secure_fault.SFSR & SAU_SFSR_INVER_Msk) != 0) {
		reason = K_ERR_ARM_SECURE_EXCEPTION_RETURN;
	} else if ((secure_fault.SFSR & SAU_SFSR_AUVIOL_Msk) != 0) {
		reason = K_ERR_ARM_SECURE_ATTRIBUTION_UNIT;
	} else if ((secure_fault.SFSR & SAU_SFSR_INVTRAN_Msk) != 0) {
		reason = K_ERR_ARM_SECURE_TRANSITION;
	} else if ((secure_fault.SFSR & SAU_SFSR_LSPERR_Msk) != 0) {
		reason = K_ERR_ARM_SECURE_LAZY_STATE_PRESERVATION;
	} else if ((secure_fault.SFSR & SAU_SFSR_LSERR_Msk) != 0) {
		reason = K_ERR_ARM_SECURE_LAZY_STATE_ERROR;
	}
	LOG_DBG("SecureFault");

	reboot_state.reason = reason;
	reboot_state.epoch_time_source = TIME_SOURCE_INVALID;
	reboot_state.param_1.program_counter = secure_fault.EXC_FRAME_COPY[ARCH_ESF_PC_IDX];
	reboot_state.param_2.link_register = secure_fault.EXC_FRAME_COPY[ARCH_ESF_LR_IDX];
	/* We can't extract the thread name, but we can output the frame pointer as a string.
	 * This should point back to the stack of the offending thread.
	 */
	snprintf(frame_ptr_str, sizeof(frame_ptr_str), "%08lx", (uintptr_t)secure_fault.EXC_FRAME);
	memcpy(reboot_state.thread_name, frame_ptr_str, sizeof(reboot_state.thread_name));

	return 0;
}

#endif /* CONFIG_TFM_PLATFORM_FAULT_INFO_QUERY */

static int infuse_common_boot(void)
{
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboot = {0};
	struct infuse_version v = application_version_get();
	bool critical_failed = false;
	int rc;
#ifdef CONFIG_INFUSE_SDK
	uint64_t device_id = infuse_device_id();
#else
	uint64_t device_id = 0;
#endif /* CONFIG_INFUSE_SDK */

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
		critical_failed = true;
	}
#endif /* CONFIG_USB_DEVICE_INITIALIZE_AT_BOOT */
#endif /* CONFIG_USB_DEVICE_STACK */
#ifdef CONFIG_INFUSE_BOARD_HAS_PUBLIC_BT_ADDRESS
	bt_addr_le_t public_addr;

	if (infuse_board_public_bt_addr(&public_addr) == 0) {
		bt_ctlr_set_public_addr(public_addr.a.val);
	}
#endif /* CONFIG_INFUSE_BOARD_HAS_PUBLIC_BT_ADDRESS */
#ifdef CONFIG_BT
	rc = bt_enable(NULL);
	if (rc) {
		LOG_ERR("Failed to enable Bluetooth (%d)", rc);
		critical_failed = true;
	}
#ifdef CONFIG_BT_CONTROLLER_MANAGER
	rc = bt_controller_manager_init();
	if (rc) {
		LOG_WRN("Failed to init controller manager (%d)", rc);
	}
#endif /* CONFIG_BT_CONTROLLER_MANAGE*/
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
#ifdef CONFIG_KV_STORE_KEY_BLUETOOTH_CTLR_VERSION
	KV_KEY_TYPE(KV_KEY_BLUETOOTH_CTLR_VERSION) bt_ctlr_ver;

	if (KV_STORE_READ(KV_KEY_BLUETOOTH_CTLR_VERSION, &bt_ctlr_ver) > 0) {
		LOG_INF("\tBT Ctlr: %d.%d.%d+%08x", bt_ctlr_ver.version.major,
			bt_ctlr_ver.version.minor, bt_ctlr_ver.version.revision,
			bt_ctlr_ver.version.build_num);
	}
#endif /* CONFIG_KV_STORE_KEY_BLUETOOTH_CTLR_VERSION */
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
#ifdef CONFIG_TFM_PLATFORM_FAULT_INFO_QUERY
		rc = secure_fault_info_read();
#endif /* CONFIG_TFM_PLATFORM_FAULT_INFO_QUERY */
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

		if (reboot_state.epoch_time_source != TIME_SOURCE_INVALID) {
			/* Restore time knowledge (Assume reboot took 0 ms) */
			reference.local = 0;
			reference.ref = reboot_state.epoch_time;
			epoch_time_set_reference(
				TIME_SOURCE_RECOVERED | reboot_state.epoch_time_source, &reference);
		}
	} else {
		LOG_INF("\t   Cause: Unknown");
	}
#else
	reboot_state.reason = INFUSE_REBOOT_UNKNOWN;
#endif /* CONFIG_INFUSE_REBOOT */

#if defined(CONFIG_NRF_MODEM_LIB) && !defined(CONFIG_NRF_MODEM_LIB_NET_IF_AUTO_START)
	/* Feed all watchdog channels before intialising the modem library, as the init can
	 * block while performing a DFU update, which can take a non-trivial amount of time.
	 * Unfortuntely there is no way to do this asynchronously, so we hope the watchdog
	 * period is configured to be long enough.
	 */
	infuse_watchdog_feed_all();
	/* nRF modems are not low power until the library has been initialised */
	LOG_DBG("Initialising nRF modem library");
	rc = nrf_modem_lib_init();
	if (rc < 0) {
		LOG_ERR("Failed to initialise nRF modem library (%d)", rc);
		critical_failed = true;
	}
#endif /* defined(CONFIG_NRF_MODEM_LIB) && !defined(CONFIG_NRF_MODEM_LIB_NET_IF_AUTO_START) */

#ifdef CONFIG_INFUSE_SECURITY
	if (infuse_security_init() < 0) {
		LOG_ERR("Failed to initialise security");
		critical_failed = true;
	}
#endif /* CONFIG_INFUSE_SECURITY */

#ifdef CONFIG_INFUSE_COMMON_BOOT_AUTO_IMG_CONFIRM
	if (critical_failed == false) {
		/* All major systems passed */
		boot_write_img_confirmed();
	}
#endif /* CONFIG_INFUSE_COMMON_BOOT_AUTO_IMG_CONFIRM */

#ifdef CONFIG_INFUSE_COMMON_BOOT_DEBUG_PORT_DISABLE
	infuse_security_disable_dap();
#endif /* CONFIG_INFUSE_COMMON_BOOT_DEBUG_PORT_DISABLE */

	(void)critical_failed;
	(void)rc;
	return 0;
}

SYS_INIT(infuse_common_boot, APPLICATION, CONFIG_INFUSE_COMMON_BOOT_INIT_PRIORITY);
