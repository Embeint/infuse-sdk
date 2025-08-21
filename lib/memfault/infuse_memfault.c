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
#include <zephyr/sys/crc.h>
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
#include "memfault/core/trace_event.h"

static char infuse_id_str[17];
static char software_version[17];

#if DT_NODE_EXISTS(DT_CHOSEN(infuse_memfault_epacket_dump))
#define DUMP_INTERFACE DEVICE_DT_GET(DT_CHOSEN(infuse_memfault_epacket_dump))

static void interface_state_cb(uint16_t current_max_payload, void *user_ctx);

static struct epacket_interface_cb epacket_cb = {
	.interface_state = interface_state_cb,
};
static struct k_work_delayable epacket_dump_work;

static void interface_state_cb(uint16_t current_max_payload, void *user_ctx)
{
	if (current_max_payload > 0) {
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

int infuse_memfault_queue_dump_all(k_timeout_t delay)
{
	if (epacket_interface_max_packet_size(DUMP_INTERFACE) == 0) {
		return -ENOTCONN;
	}
	if (!memfault_packetizer_data_available()) {
		return -ENODATA;
	}
	k_work_schedule(&epacket_dump_work, delay);
	return 0;
}

#endif /* DT_NODE_EXISTS(DT_CHOSEN(infuse_memfault_epacket_dump)) */

#ifndef CONFIG_MEMFAULT_CRC16_BUILTIN

uint16_t memfault_crc16_compute(uint16_t crc_initial_value, const void *data, size_t data_len_bytes)
{
	return crc16_itu_t(crc_initial_value, data, data_len_bytes);
}

#endif

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
		.software_type = CONFIG_MEMFAULT_INFUSE_SOFTWARE_TYPE,
		.software_version = software_version,
		.hardware_version = CONFIG_MEMFAULT_INFUSE_HARDWARE_VERSION,
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

#ifdef CONFIG_MEMFAULT_INFUSE_SECURE_FAULT_KNOWLEDGE

#include "../core/src/memfault_reboot_tracking_private.h"

static struct k_work_delayable secure_fault_trace;

#ifndef CONFIG_TFM_PLATFORM_FAULT_INFO_QUERY
BUILD_ASSERT(IS_ENABLED(CONFIG_ZTEST));

struct fault_exception_info_t {
	uint32_t VECTACTIVE;           /* Active exception number. */
	uint32_t EXC_RETURN;           /* EXC_RETURN value in LR. */
	uint32_t MSP;                  /* (Secure) MSP. */
	uint32_t PSP;                  /* (Secure) PSP. */
	uint32_t *EXC_FRAME;           /* Exception frame on stack. */
	uint32_t EXC_FRAME_COPY[8];    /* Copy of the basic exception frame. */
	uint32_t CALLEE_SAVED_COPY[8]; /* Copy of the callee saved registers. */
	uint32_t xPSR;                 /* Program Status Registers. */
	uint32_t CFSR;                 /* Configurable Fault Status Register. */
	uint32_t HFSR;                 /* Hard Fault Status Register. */
	uint32_t BFAR;                 /* Bus Fault address register. */
	uint32_t BFARVALID;            /* Whether BFAR contains a valid address. */
	uint32_t MMFAR;                /* MemManage Fault address register. */
	uint32_t MMARVALID;            /* Whether MMFAR contains a valid address. */
	uint32_t SFSR;                 /* SecureFault Status Register. */
	uint32_t SFAR;                 /* SecureFault Address Register. */
	uint32_t SFARVALID;            /* Whether SFAR contains a valid address. */
} __packed;

int infuse_common_boot_secure_fault_info(struct fault_exception_info_t *fault_info)
{
	memset(fault_info, 0x00, sizeof(*fault_info));
	return 0;
}

#endif /* !CONFIG_TFM_PLATFORM_FAULT_INFO_QUERY */

#define MEMFAULT_REBOOT_INFO_MAGIC   0x21544252
#define MEMFAULT_REBOOT_INFO_VERSION 2

/* Ensure that the two fault frames match */
BUILD_ASSERT(sizeof(((struct arch_esf *)(NULL))->basic) ==
	     sizeof(((struct fault_exception_info_t *)(NULL))->EXC_FRAME_COPY));

#define ARCH_ESF_R0_IDX (offsetof(struct arch_esf, basic.r0) / sizeof(uint32_t))
#define ARCH_ESF_R1_IDX (offsetof(struct arch_esf, basic.r1) / sizeof(uint32_t))
#define ARCH_ESF_R2_IDX (offsetof(struct arch_esf, basic.r2) / sizeof(uint32_t))
#define ARCH_ESF_R3_IDX (offsetof(struct arch_esf, basic.r3) / sizeof(uint32_t))
#define ARCH_ESF_PC_IDX (offsetof(struct arch_esf, basic.pc) / sizeof(uint32_t))
#define ARCH_ESF_LR_IDX (offsetof(struct arch_esf, basic.lr) / sizeof(uint32_t))

BUILD_ASSERT(CONFIG_MEMFAULT_INIT_PRIORITY > CONFIG_INFUSE_COMMON_BOOT_INIT_PRIORITY,
	     "Memfault init must run after common_boot");

static void memfault_secure_fault_trace(struct k_work *work)
{
	struct fault_exception_info_t fault_info;
	uint8_t SFSR;

	/* Pull the complete fault frame */
	infuse_common_boot_secure_fault_info(&fault_info);

	/* SFSR only has 8 bits of defined information */
	SFSR = fault_info.SFSR;

	/* Push additional fault information as a trace event */
	memfault_trace_event_with_log_capture(
		MEMFAULT_TRACE_REASON(secure_fault),
		(void *)fault_info.EXC_FRAME_COPY[ARCH_ESF_PC_IDX],
		(void *)fault_info.EXC_FRAME_COPY[ARCH_ESF_LR_IDX],
		"R0-3 %08x %08x %08x %08x EXC %08x xPSR %08x SFSR %02x SFAR %08x",
		fault_info.EXC_FRAME_COPY[ARCH_ESF_R0_IDX],
		fault_info.EXC_FRAME_COPY[ARCH_ESF_R1_IDX],
		fault_info.EXC_FRAME_COPY[ARCH_ESF_R2_IDX],
		fault_info.EXC_FRAME_COPY[ARCH_ESF_R3_IDX], (uint32_t)fault_info.EXC_FRAME,
		fault_info.xPSR, SFSR, fault_info.SFAR);
}

void memfault_reboot_tracking_load(sMemfaultRebootTrackingStorage *dst)
{
	sMfltRebootInfo *reboot_info = (sMfltRebootInfo *)dst;
	struct infuse_reboot_state infuse_reboot;

	if (infuse_common_boot_last_reboot(&infuse_reboot) != 0) {
		/* No reboot knowledge, therefore no secure fault knowledge */
		return;
	}
	if (!IN_RANGE(infuse_reboot.reason, (uint8_t)K_ERR_ARM_SECURE_GENERIC,
		      (uint8_t)K_ERR_ARM_SECURE_LAZY_STATE_ERROR)) {
		/* Not a secure fault, Memfault should already know about it */
		return;
	}

	/* Provide Memfault the information we know about the secure fault */
	*reboot_info = (sMfltRebootInfo){
		.magic = MEMFAULT_REBOOT_INFO_MAGIC,
		.version = MEMFAULT_REBOOT_INFO_VERSION,
		.last_reboot_reason = kMfltRebootReason_SecurityViolation,
		.pc = infuse_reboot.info.exception_basic.program_counter,
		.lr = infuse_reboot.info.exception_basic.link_register,
	};
#ifdef CONFIG_ARM
	if (infuse_reboot.info_type == INFUSE_REBOOT_INFO_EXCEPTION_ESF) {
		/* Stack frame instead of basic ESF */
		reboot_info->pc = infuse_reboot.info.exception_full.basic.pc;
		reboot_info->lr = infuse_reboot.info.exception_full.basic.lr;
	}
#endif /* CONFIG_ARM */

	/* Defer logging of the secure fault trace event until after Memfault has finished
	 * initialising. Use a delayable worked since an immediate `k_work_submit` will just
	 * run the work before we have left the function.
	 */
	k_work_init_delayable(&secure_fault_trace, memfault_secure_fault_trace);
	k_work_schedule(&secure_fault_trace, K_SECONDS(1));
}

#endif /* CONFIG_MEMFAULT_INFUSE_SECURE_FAULT_KNOWLEDGE */

/* Copied from memfault/ports/zephyr/common/memfault_platform_core.c
 * Usage complies with Memfault license as this is only ever used for integration with Memfault
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
	case K_ERR_ARM_SECURE_GENERIC:
	case K_ERR_ARM_SECURE_ENTRY_POINT:
	case K_ERR_ARM_SECURE_INTEGRITY_SIGNATURE:
	case K_ERR_ARM_SECURE_EXCEPTION_RETURN:
	case K_ERR_ARM_SECURE_ATTRIBUTION_UNIT:
	case K_ERR_ARM_SECURE_TRANSITION:
	case K_ERR_ARM_SECURE_LAZY_STATE_PRESERVATION:
	case K_ERR_ARM_SECURE_LAZY_STATE_ERROR:
		info->reset_reason = kMfltRebootReason_SecurityViolation;
		break;
#endif /* CONFIG_ARM */
	case INFUSE_REBOOT_RPC:
	case INFUSE_REBOOT_CFG_CHANGE:
		info->reset_reason = kMfltRebootReason_UserReset;
		break;
	case INFUSE_REBOOT_DFU:
	case INFUSE_REBOOT_MCUMGR:
		info->reset_reason = kMfltRebootReason_FirmwareUpdate;
		break;
	case INFUSE_REBOOT_EXTERNAL_TRIGGER:
		info->reset_reason = kMfltRebootReason_ButtonReset;
		break;
	case INFUSE_REBOOT_HW_WATCHDOG:
		info->reset_reason = kMfltRebootReason_HardwareWatchdog;
		break;
	case INFUSE_REBOOT_SW_WATCHDOG:
		info->reset_reason = kMfltRebootReason_SoftwareWatchdog;
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

	/* No data to dump */
	if (!memfault_packetizer_data_available()) {
		return true;
	}

	while (true) {
		if (tx == NULL) {
			tx = epacket_alloc_tx_for_interface(dev, K_NO_WAIT);
			if (tx == NULL) {
				/* Still work to do, but no buffers remaining */
				return false;
			}
			if (net_buf_tailroom(tx) == 0) {
				/* Interface has gone down, free the buffer and report complete */
				net_buf_unref(tx);
				return true;
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
			epacket_set_tx_metadata(tx, EPACKET_AUTH_DEVICE, 0, INFUSE_MEMFAULT_CHUNK,
						EPACKET_ADDR_ALL);
			epacket_queue(dev, tx);

			/* Need another packet allocated */
			tx = NULL;
		}
	}

	/* If there is a pending packet, send it */
	if (tx) {
		/* Set packet metadata and queue for transmission */
		epacket_set_tx_metadata(tx, EPACKET_AUTH_DEVICE, 0, INFUSE_MEMFAULT_CHUNK,
					EPACKET_ADDR_ALL);
		epacket_queue(dev, tx);
	}

	/* All packets dumped */
	return true;
}

#endif /* CONFIG_EPACKET */

SYS_INIT(infuse_memfault_platform_init, APPLICATION, 0);
