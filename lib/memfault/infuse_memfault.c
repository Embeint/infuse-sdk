/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <string.h>
#include <stdio.h>

#include <zephyr/init.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/fatal_types.h>
#include <zephyr/sys/__assert.h>

#include <infuse/version.h>
#include <infuse/identifiers.h>
#include <infuse/common_boot.h>
#include <infuse/time/epoch.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/lib/memfault.h>

#include "memfault/core/platform/device_info.h"
#include "memfault/core/platform/system_time.h"
#include "memfault/core/data_packetizer.h"
#include "memfault/core/reboot_tracking.h"

static char infuse_id_str[17];
static char software_version[17];

#if DT_NODE_EXISTS(DT_CHOSEN(infuse_memfault_epacket_dump))
#define DUMP_INTERFACE DEVICE_DT_GET(DT_CHOSEN(infuse_memfault_epacket_dump))

static void interface_state_cb(bool connected, uint16_t current_max_payload, void *user_ctx);

static struct epacket_interface_cb epacket_cb = {
	.interface_state = interface_state_cb,
};
static struct k_work_delayable epacket_dump_work;

static void interface_state_cb(bool connected, uint16_t current_max_payload, void *user_ctx)
{
	if (connected) {
		k_work_schedule(&epacket_dump_work, K_NO_WAIT);
	}
}

static void epacket_dump_fn(struct k_work *work)
{
	if (!infuse_memfault_dump_chunks_epacket(DUMP_INTERFACE)) {
		/* Dumping not complete, run again shortly */
		k_work_reschedule(&epacket_dump_work, K_MSEC(20));
	}
}

#endif /* DT_NODE_EXISTS(DT_CHOSEN(infuse_memfault_epacket_dump)) */

int infuse_memfault_platform_init(void)
{
	struct infuse_version version = application_version_get();
	uint64_t infuse_id = infuse_device_id();

	snprintf(infuse_id_str, sizeof(infuse_id_str), "%016" PRIx64, infuse_id);
	snprintf(software_version, sizeof(software_version),
		 "%" PRIu8 ".%" PRIu8 ".%" PRIu16 "+%08" PRIx32, version.major, version.minor,
		 version.revision, version.build_num);

#if DT_NODE_EXISTS(DT_CHOSEN(infuse_memfault_epacket_dump))
	k_work_init_delayable(&epacket_dump_work, epacket_dump_fn);
	/* Register for callbacks on state change */
	epacket_register_callback(DUMP_INTERFACE, &epacket_cb);
#endif /* DT_NODE_EXISTS(DT_CHOSEN(infuse_memfault_epacket_dump)) */
	return 0;
}

void memfault_platform_get_device_info(sMemfaultDeviceInfo *info)
{
	*info = (sMemfaultDeviceInfo){
		.device_serial = infuse_id_str,
		.software_type = "app",
		.software_version = software_version,
		.hardware_version = CONFIG_BOARD_TARGET,
	};
}

bool memfault_platform_time_get_current(sMemfaultCurrentTime *current_time)
{
	if (!epoch_time_trusted_source(epoch_time_get_source(), true)) {
		return false;
	}
	current_time->type = kMemfaultCurrentTimeType_UnixEpochTimeSec;
	current_time->info.unix_timestamp_secs = unix_time_from_epoch(epoch_time_now());
	return true;
}

/* Copied from memfault/ports/zephyr/common/memfault_platform_core.c
 * Usage complies with Memfault license as this is only every used for integration with Memfault
 * services.
 * Copyright (c) Memfault, Inc.
 */
static eMemfaultRebootReason zephyr_to_memfault_reboot_reason(uint32_t reset_reason_reg)
{
	eMemfaultRebootReason reset_reason = kMfltRebootReason_Unknown;
	const struct hwinfo_bit_to_memfault_reset_reason {
		uint16_t hwinfo_bit;
		uint16_t memfault_reason;
	} s_hwinfo_to_memfault[] = {
		{RESET_PIN, kMfltRebootReason_PinReset},
		{RESET_SOFTWARE, kMfltRebootReason_SoftwareReset},
		{RESET_BROWNOUT, kMfltRebootReason_BrownOutReset},
		{RESET_POR, kMfltRebootReason_PowerOnReset},
		{RESET_WATCHDOG, kMfltRebootReason_HardwareWatchdog},
		{RESET_DEBUG, kMfltRebootReason_DebuggerHalted},
		{RESET_SECURITY, kMfltRebootReason_SecurityViolation},
		{RESET_LOW_POWER_WAKE, kMfltRebootReason_LowPower},
		{RESET_CPU_LOCKUP, kMfltRebootReason_Lockup},
		{RESET_PARITY, kMfltRebootReason_ParityError},
		{RESET_PLL, kMfltRebootReason_ClockFailure},
		{RESET_CLOCK, kMfltRebootReason_ClockFailure},
		{RESET_HARDWARE, kMfltRebootReason_Hardware},
		{RESET_USER, kMfltRebootReason_UserReset},
		{RESET_TEMPERATURE, kMfltRebootReason_Temperature},
	};

	for (size_t i = 0; i < ARRAY_SIZE(s_hwinfo_to_memfault); i++) {
		if (reset_reason_reg & (uint32_t)s_hwinfo_to_memfault[i].hwinfo_bit) {
			reset_reason =
				(eMemfaultRebootReason)s_hwinfo_to_memfault[i].memfault_reason;
			break;
		}
	}

	return reset_reason;
}

void memfault_reboot_reason_get(sResetBootupInfo *info)
{
	struct infuse_reboot_state state;

	/* Reason and hardware registers are valid regardless of return value */
	(void)infuse_common_boot_last_reboot(&state);

	/* Convert to Memfault values */
	info->reset_reason_reg = state.hardware_reason;
	switch ((uint8_t)state.reason) {
	case K_ERR_CPU_EXCEPTION:
		info->reset_reason = kMfltRebootReason_Nmi;
		break;
	case K_ERR_KERNEL_OOPS:
	case K_ERR_KERNEL_PANIC:
		info->reset_reason = kMfltRebootReason_Assert;
		break;
	case K_ERR_STACK_CHK_FAIL:
		info->reset_reason = kMfltRebootReason_StackOverflow;
		break;
#ifdef CONFIG_ARM
	case K_ERR_ARM_BUS_GENERIC:
	case K_ERR_ARM_BUS_STACKING:
	case K_ERR_ARM_BUS_UNSTACKING:
	case K_ERR_ARM_BUS_PRECISE_DATA_BUS:
	case K_ERR_ARM_BUS_IMPRECISE_DATA_BUS:
	case K_ERR_ARM_BUS_INSTRUCTION_BUS:
	case K_ERR_ARM_BUS_FP_LAZY_STATE_PRESERVATION:
		info->reset_reason = kMfltRebootReason_BusFault;
		break;
	case K_ERR_ARM_MEM_GENERIC:
	case K_ERR_ARM_MEM_STACKING:
	case K_ERR_ARM_MEM_UNSTACKING:
	case K_ERR_ARM_MEM_DATA_ACCESS:
	case K_ERR_ARM_MEM_INSTRUCTION_ACCESS:
	case K_ERR_ARM_MEM_FP_LAZY_STATE_PRESERVATION:
		info->reset_reason = kMfltRebootReason_MemFault;
		break;
	case K_ERR_ARM_USAGE_GENERIC:
	case K_ERR_ARM_USAGE_DIV_0:
	case K_ERR_ARM_USAGE_UNALIGNED_ACCESS:
	case K_ERR_ARM_USAGE_STACK_OVERFLOW:
	case K_ERR_ARM_USAGE_NO_COPROCESSOR:
	case K_ERR_ARM_USAGE_ILLEGAL_EXC_RETURN:
	case K_ERR_ARM_USAGE_ILLEGAL_EPSR:
	case K_ERR_ARM_USAGE_UNDEFINED_INSTRUCTION:
		info->reset_reason = kMfltRebootReason_UsageFault;
		break;
#endif /* CONFIG_ARM */
	case INFUSE_REBOOT_RPC:
		info->reset_reason = kMfltRebootReason_UserReset;
		break;
	case INFUSE_REBOOT_DFU:
	case INFUSE_REBOOT_MCUMGR:
		info->reset_reason = kMfltRebootReason_FirmwareUpdate;
		break;
	case INFUSE_REBOOT_EXTERNAL_TRIGGER:
		info->reset_reason = kMfltRebootReason_ButtonReset;
		break;
	default:
		info->reset_reason = zephyr_to_memfault_reboot_reason(state.hardware_reason);
	}
}

#ifdef CONFIG_EPACKET

bool infuse_memfault_dump_chunks_epacket(const struct device *dev)
{
	static uint8_t chunk_counter;
	struct memfault_chunk_header *header;
	struct net_buf *tx = NULL;
	bool data_available;
	size_t buf_len;

	while (true) {
		if (tx == NULL) {
			tx = epacket_alloc_tx_for_interface(dev, K_NO_WAIT);
			if (tx == NULL) {
				/* Still work to do, but no buffers remaining */
				return false;
			}
		}

		/* Push header */
		header = net_buf_add(tx, sizeof(*header));
		buf_len = net_buf_tailroom(tx);

		/* Pull data from packetizer */
		data_available = memfault_packetizer_get_chunk(net_buf_tail(tx), &buf_len);
		if (!data_available) {
			/* No data left */
			net_buf_remove_mem(tx, sizeof(*header));
			break;
		}

		if (buf_len == 0) {
			net_buf_remove_mem(tx, sizeof(*header));
		} else {
			/* Update net buffer size */
			net_buf_add(tx, buf_len);
			/* Update header */
			header->chunk_cnt = chunk_counter++;
			header->chunk_len = buf_len;
		}

		/* Send packet if there is not enough space for another chunk or no data was added
		 * on this iteration
		 */
		if ((net_buf_tailroom(tx) < (sizeof(*header) + MEMFAULT_PACKETIZER_MIN_BUF_LEN)) ||
		    (buf_len == 0)) {
			/* Set packet metadata and queue for transmission */
			epacket_set_tx_metadata(tx, EPACKET_AUTH_DEVICE, 0, INFUSE_MEMFAULT_CHUNK);
			epacket_queue(dev, tx);

			/* Need another packet allocated */
			tx = NULL;
		}
	}

	/* If there is a pending packet, send it */
	if (tx) {
		/* Set packet metadata and queue for transmission */
		epacket_set_tx_metadata(tx, EPACKET_AUTH_DEVICE, 0, INFUSE_MEMFAULT_CHUNK);
		epacket_queue(dev, tx);
	}

	/* All packets dumped */
	return true;
}

#endif /* CONFIG_EPACKET */

SYS_INIT(infuse_memfault_platform_init, APPLICATION, 0);
