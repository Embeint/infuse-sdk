/**
 * @file
 * @brief Reboot handling for Infuse-IoT applications
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
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

/* Enumeration of reboot reasons */
enum infuse_reboot_reason {
	/* Common Zephyr exception codes */
	INFUSE_REBOOT_ZEPHYR_BASE = K_ERR_CPU_EXCEPTION,
	/* Architecture specific exception codes */
	INFUSE_REBOOT_ARCH_SPECIFIC = K_ERR_ARCH_START,
	/* Infuse reboot reasons */
	INFUSE_REBOOT_INFUSE_START = 128,
	/* Watchdog has expired */
	INFUSE_REBOOT_WATCHDOG = INFUSE_REBOOT_INFUSE_START,
	/* Triggered externally (Button, etc) */
	INFUSE_REBOOT_EXTERNAL_TRIGGER,
	/* Remote procedure call */
	INFUSE_REBOOT_RPC,
	/* Unknown reboot reason */
	INFUSE_REBOOT_UNKNOWN = 255,
};

/* Reboot state information */
struct infuse_reboot_state {
	/* First 3 parameters are updated a second time on delayed reboots.
	 * Do not modify the order.
	 */

	/* The source of civil time */
	uint8_t civil_time_source;
	/* The civil time at the reboot */
	uint64_t civil_time;
	/* The device uptime at the reboot */
	uint32_t uptime;
	/* Reason for the reboot */
	enum infuse_reboot_reason reason;
	/* Hardware reboot reason flags */
	uint32_t hardware_reason;
	union {
		/* Program counter value at exception */
		uint32_t program_counter;
		/* Watchdog channel that expired */
		uint32_t watchdog_channel;
	} param_1;
	union {
		/* Link register value at exception */
		uint32_t link_register;
	} param_2;
	/* Thread executing at reboot time */
	char thread_name[REBOOT_STATE_THREAD_NAME_MAX];
} __packed;

/**
 * @brief Trigger a system reboot
 *
 * @param reason Reason the system is rebooting
 * @param info1 Program counter at exception or watchdog channel that expired
 * @param info2 Link register at exception
 */
FUNC_NORETURN void infuse_reboot(enum infuse_reboot_reason reason, uint32_t info1, uint32_t info2);

/**
 * @brief Trigger a system reboot in the future
 *
 * @param reason Reason the system is rebooting
 * @param info1 Program counter at exception or watchdog channel that expired
 * @param info2 Link register at exception
 * @param delay Time delay or absolute time to execute the reboot
 */
void infuse_reboot_delayed(enum infuse_reboot_reason reason, uint32_t info1, uint32_t info2,
			   k_timeout_t delay);

/**
 * @brief Query the reason for the previous reboot
 *
 * @warning Will only return valid state on the first call.
 *
 * @param state State storage area
 *
 * @retval 0 On successful state query
 * @retval -ENOENT No stored state exists
 */
int infuse_reboot_state_query(struct infuse_reboot_state *state);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_REBOOT_H_ */
