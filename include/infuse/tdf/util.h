/**
 * @file
 * @brief Utility TDF helpers
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TDF_UTIL_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TDF_UTIL_H_

#include <stdint.h>
#include <string.h>

#include <zephyr/bluetooth/addr.h>

#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>
#include <infuse/reboot.h>
#include <infuse/common_boot.h>
#include <infuse/time/epoch.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/lib/nrf_modem_monitor.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup tdf_util_apis TDF util APIs
 * @{
 */

/**
 * @brief Get TDF ID to use for given accelerometer full scale range
 *
 * @param range Maximum data range in Gs
 *
 * @return Appropriate TDF ID
 */
static inline uint16_t tdf_id_from_accelerometer_range(uint8_t range)
{
	switch (range) {
	case 2:
		return TDF_ACC_2G;
	case 4:
		return TDF_ACC_4G;
	case 8:
		return TDF_ACC_8G;
	default:
		return TDF_ACC_16G;
	}
}

/**
 * @brief Get TDF ID to use for given gyroscope full scale range
 *
 * @param range Maximum data range in DPS
 *
 * @return Appropriate TDF ID
 */
static inline uint16_t tdf_id_from_gyroscope_range(uint16_t range)
{
	switch (range) {
	case 125:
		return TDF_GYR_125DPS;
	case 250:
		return TDF_GYR_250DPS;
	case 500:
		return TDF_GYR_500DPS;
	case 1000:
		return TDF_GYR_1000DPS;
	default:
		return TDF_GYR_2000DPS;
	}
}

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
	info->count = reboot.count;
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
 * @brief Populate the TDF Bluetooth address from a Bluetooth stack structure
 *
 * @param addr Bluetooth stack address
 * @param tdf TDF address structure
 */
static inline void tdf_bt_addr_le_from_stack(const bt_addr_le_t *addr,
					     struct tdf_struct_bt_addr_le *tdf)
{
	tdf->type = addr->type;
	memcpy(tdf->val, addr->a.val, 6);
}

/**
 * @brief Populate the LTE connection status TDF from modem monitor information
 *
 * @param network_state Modem monitor network state
 * @param tdf TDF state structure
 * @param rsrp Reference signal received power (dBm) (INT16_MIN if unknown)
 * @param rsrq Reference signal received quality (dB) (INT8_MIN if unknown)
 */
static inline void tdf_lte_conn_status_from_monitor(struct nrf_modem_network_state *network_state,
						    struct tdf_lte_conn_status *tdf, int16_t rsrp,
						    int8_t rsrq)
{
	tdf->cell.mcc = network_state->cell.mcc;
	tdf->cell.mnc = network_state->cell.mnc;
	tdf->cell.tac = network_state->cell.tac;
	tdf->cell.eci = network_state->cell.id;
	tdf->status = network_state->nw_reg_status;
	tdf->tech = network_state->lte_mode;
	tdf->earfcn = network_state->cell.earfcn;
	tdf->rsrq = rsrq;
	tdf->rsrp = UINT8_MAX;
	if (rsrp != INT16_MIN) {
		tdf->rsrp = 0 - rsrp;
	}
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
#endif /* CONFIG_TDF_UTIL_REBOOT_INFO_LOG */
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TDF_UTIL_H_ */
