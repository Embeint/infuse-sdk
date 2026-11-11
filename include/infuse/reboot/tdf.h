/**
 * @file
 * @brief Reboot TDF helpers
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_REBOOT_TDF_H_
#define INFUSE_SDK_INCLUDE_INFUSE_REBOOT_TDF_H_

#include <stdint.h>
#include <string.h>

#include <infuse/common_boot.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/reboot.h>
#include <infuse/tdf/definitions.h>
#include <infuse/time/epoch.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reboot TDF helpers
 * @defgroup reboot_tdf_apis Reboot TDF helpers
 * @ingroup infuse_reboot_apis
 * @{
 */

/**
 * @brief Populate a REBOOT_INFO TDF from Infuse-IoT reboot state
 *
 * @param state Infuse-IoT reboot state pointer
 * @param info REBOOT_INFO TDF pointer
 */
static inline void tdf_reboot_info_from_state(struct infuse_reboot_state *state,
					      struct tdf_reboot_info *info)
{
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboot = {0};

#ifdef CONFIG_KV_STORE
	KV_STORE_READ(KV_KEY_REBOOTS, &reboot);
#endif

	info->reason = state->reason;
	info->hardware_flags = state->hardware_reason;
	info->uptime = state->uptime;
	info->count = reboot.count;
	if (state->info_type == INFUSE_REBOOT_INFO_EXCEPTION_ESF) {
#ifdef CONFIG_ARM
		info->param_1 = state->info.exception_full.basic.pc;
		info->param_2 = state->info.exception_full.basic.lr;
#else
		/* Decoding ESF's is arch specific */
		info->param_1 = 0;
		info->param_2 = 0;
#endif /* CONFIG_ARM */
	} else {
		/* Generic, Exception Basic, Watchdog all have the same info layout */
		info->param_1 = state->info.generic.info1;
		info->param_2 = state->info.generic.info2;
	}
	strncpy(info->thread, state->thread_name, sizeof(info->thread));
}

/**
 * @brief Log REBOOT_INFO TDF to specified TDF data loggers
 *
 * TDF is populated from @ref infuse_common_boot_last_reboot.
 *
 * @param logger_mask TDF data loggers to write to
 */
static inline void tdf_reboot_info_log(uint8_t logger_mask)
{
#ifdef CONFIG_TDF_UTIL_REBOOT_INFO_LOG
	struct infuse_reboot_state reboot_state;
	struct tdf_reboot_info reboot_info;
	uint64_t t = epoch_time_from_ticks(0);

	/* Construct reboot info TDF */
	infuse_common_boot_last_reboot(&reboot_state);
	tdf_reboot_info_from_state(&reboot_state, &reboot_info);
	/* Push TDF at logger */
	TDF_DATA_LOGGER_LOG(logger_mask, TDF_REBOOT_INFO, t, &reboot_info);
	if (reboot_state.info_type == INFUSE_REBOOT_INFO_EXCEPTION_ESF) {
		/* Except word aligned exception stack frames */
		BUILD_ASSERT(sizeof(reboot_state.info.exception_full) % sizeof(uint32_t) == 0);
		/* Push full exception stack frame at the logger */
		tdf_data_logger_log(logger_mask, TDF_EXCEPTION_STACK_FRAME,
				    sizeof(reboot_state.info.exception_full), t,
				    &reboot_state.info.exception_full);
	}
#else
	ARG_UNUSED(logger_mask);
#endif /* CONFIG_TDF_UTIL_REBOOT_INFO_LOG */
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_REBOOT_TDF_H_ */
