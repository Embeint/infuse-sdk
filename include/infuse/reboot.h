/**
 * @file
 * @brief Reboot handling for Infuse-IoT applications
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_REBOOT_H_
#define INFUSE_SDK_INCLUDE_INFUSE_REBOOT_H_

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/fatal_types.h>
#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Infuse reboot API
 * @defgroup infuse_reboot_apis Infuse reboot APIs
 * @{
 */

#define REBOOT_STATE_THREAD_NAME_MAX 8

/** Enumeration of reboot reasons */
enum infuse_reboot_reason {
	/** Common Zephyr exception codes */
	INFUSE_REBOOT_ZEPHYR_BASE = K_ERR_CPU_EXCEPTION,
	/** Architecture specific exception codes */
	INFUSE_REBOOT_ARCH_SPECIFIC = K_ERR_ARCH_START,
	/** Infuse reboot reasons */
	INFUSE_REBOOT_INFUSE_START = 128,
	/** Hardware watchdog has expired */
	INFUSE_REBOOT_HW_WATCHDOG = 128,
	/** Triggered externally (Button, etc) */
	INFUSE_REBOOT_EXTERNAL_TRIGGER = 129,
	/** Remote procedure call */
	INFUSE_REBOOT_RPC = 130,
	/** Internal LTE modem fault */
	INFUSE_REBOOT_LTE_MODEM_FAULT = 131,
	/** MCUmgr request */
	INFUSE_REBOOT_MCUMGR = 132,
	/** Rebooting due to configuration change */
	INFUSE_REBOOT_CFG_CHANGE = 133,
	/** Software watchdog has expired */
	INFUSE_REBOOT_SW_WATCHDOG = 134,
	/** Rebooting for device firmware update */
	INFUSE_REBOOT_DFU = 135,
	/** Bluetooth controller fault */
	INFUSE_REBOOT_BT_CTLR_FAULT = 136,
	/** Unknown reboot reason */
	INFUSE_REBOOT_UNKNOWN = 255,
} __packed;

/** Type of @ref infuse_reboot_info data */
enum infuse_reboot_info_type {
	/* Generic reboot, two uint32_t parameters */
	INFUSE_REBOOT_INFO_GENERIC = 0,
	/** Exception with only PC and LR info */
	INFUSE_REBOOT_INFO_EXCEPTION_BASIC,
	/** Exception with full stack frame */
	INFUSE_REBOOT_INFO_EXCEPTION_ESF,
	/** Hardware watchdog expiry */
	INFUSE_REBOOT_INFO_WATCHDOG,
} __packed;

/**
 * Detailed information about the reboot location/cause
 * RISCV platforms have 16 byte alignment requirements for @a arch_esf.
 * Still use __packed so we can guarantee the compiler does not shift memory layout.
 */
union infuse_reboot_info {
	/* Generic reboot information */
	struct {
		/** Info 1 */
		uint32_t info1;
		/** Info 2 */
		uint32_t info2;
	} generic;
	/* Basic exception information */
	struct {
		/** Program counter value at exception */
		uint32_t program_counter;
		/** Link register value at exception */
		uint32_t link_register;
	} exception_basic;
	/* Exception stack frame */
	struct arch_esf exception_full;
	/* Watchdog reboot information */
	struct {
		/** Watchdog info1 per @ref infuse_watchdog_thread_state_lookup */
		uint32_t info1;
		/** Watchdog info2 per @ref infuse_watchdog_thread_state_lookup */
		uint32_t info2;
	} watchdog;
} __packed __aligned(16);

/** Reboot state information */
struct infuse_reboot_state {
	/** First 3 parameters are updated a second time on delayed reboots.
	 * Do not modify the order.
	 */

	/** The source of epoch time */
	uint8_t epoch_time_source;
	/** The epoch time at the reboot */
	uint64_t epoch_time;
	/** The device uptime at the reboot */
	uint32_t uptime;
	/** Reason for the reboot */
	enum infuse_reboot_reason reason;
	/** Hardware reboot reason flags */
	uint32_t hardware_reason;
	/** Thread executing at reboot time */
	char thread_name[REBOOT_STATE_THREAD_NAME_MAX];
	/** Type of the information in @a info */
	enum infuse_reboot_info_type info_type;
	/** Pad the scructure out so that @a info is 16 byte aligned */
	uint8_t _padding[5];
	/** Reboot information */
	union infuse_reboot_info info;
} __packed __aligned(16);

/** @cond INTERNAL_HIDDEN */
#define _NORETURN COND_CODE_1(CONFIG_INFUSE_REBOOT_RETURN, (), (FUNC_NORETURN))
/** @endcond */

/**
 * @brief Trigger a system reboot
 *
 * @param reason Reason the system is rebooting
 * @param info1 Generic information to identify/diagnose the reboot
 * @param info2 Generic information to identify/diagnose the reboot
 */
_NORETURN void infuse_reboot(enum infuse_reboot_reason reason, uint32_t info1, uint32_t info2);

/**
 * @brief Trigger a system reboot in the future
 *
 * @param reason Reason the system is rebooting
 * @param info1 Generic information to identify/diagnose the reboot
 * @param info2 Generic information to identify/diagnose the reboot
 * @param delay Time delay or absolute time to execute the reboot
 */
void infuse_reboot_delayed(enum infuse_reboot_reason reason, uint32_t info1, uint32_t info2,
			   k_timeout_t delay);

/**
 * @brief Query the reason for the previous reboot
 *
 * If this function returns 0, state->hardware_reason contains the reboot
 * reason and the hardware register values are cleared.
 *
 * @warning Will only return valid state on the first call.
 *
 * @param state State storage area
 *
 * @retval 0 On successful state query
 * @retval -ENOENT No stored state exists
 * @retval -errno Other error from @a retention_read
 */
int infuse_reboot_state_query(struct infuse_reboot_state *state);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_REBOOT_H_ */
