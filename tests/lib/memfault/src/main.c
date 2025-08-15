/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/ztest.h>
#include <zephyr/random/random.h>

#include <infuse/common_boot.h>
#include <infuse/time/epoch.h>
#include <infuse/types.h>
#include <infuse/reboot.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/rpc/types.h>
#include <zephyr/drivers/hwinfo.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>

#include <infuse/lib/memfault.h>
#include <infuse/reboot.h>

#include "memfault/core/platform/system_time.h"
#include "memfault/core/reboot_tracking.h"
#include "memfault/ports/reboot_reason.h"

static void send_fault_command(uint32_t request_id, uint8_t fault)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_fault_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_FAULT,
			},
		.fault = fault,
	};

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static void expect_memfault_chunks(bool self_dump, size_t min_data, size_t max_data)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct memfault_chunk_header *chunk_header;
	struct net_buf *rsp;
	uint8_t *response;
	size_t remaining, received = 0;
	bool done = false;
	uint8_t counter = 0;

	zassert_not_null(response_queue);

	while (!done) {
		if (self_dump) {
			done = infuse_memfault_dump_chunks_epacket(epacket_dummy);
		} else {
			done = true;
		}

		/* Pull responses */
		while (true) {
			/* Response was sent */
			rsp = k_fifo_get(response_queue, K_MSEC(1000));
			if (rsp == NULL) {
				break;
			}

			response = (void *)(rsp->data + sizeof(struct epacket_dummy_frame));
			remaining = rsp->len - sizeof(struct epacket_dummy_frame);

			while (remaining > 0) {
				chunk_header = (void *)response;

				zassert_equal(counter, chunk_header->chunk_cnt,
					      "Chunk counter not incrementing");
				zassert_true(chunk_header->chunk_len <=
						     (remaining - sizeof(*chunk_header)),
					     "Chunk goes over packet");

				received += chunk_header->chunk_len;

				/* Move along buffer */
				remaining -= sizeof(*chunk_header) + chunk_header->chunk_len;
				response += sizeof(*chunk_header) + chunk_header->chunk_len;
				counter++;
			}

			/* Free the response */
			net_buf_unref(rsp);
		}
	}

	zassert_between_inclusive(received, min_data, max_data,
				  "Unexpected amount of Memfault chunks");

	/* Additional calls return true, no pending data */
	zassert_true(infuse_memfault_dump_chunks_epacket(epacket_dummy));
	zassert_is_null(k_fifo_get(response_queue, K_MSEC(1000)));
}

static void reboot_reason_test(uint8_t zephyr_reason, uint32_t hw_flags,
			       eMemfaultRebootReason memfault_reason)
{
	extern struct infuse_reboot_state reboot_state;
	sResetBootupInfo info;

	reboot_state.reason = zephyr_reason;
	reboot_state.hardware_reason = hw_flags;
	memfault_reboot_reason_get(&info);

	zassert_equal(memfault_reason, info.reset_reason);
}

ZTEST(memfault_integration, test_memfault_reboot_reason_get)
{

	reboot_reason_test(K_ERR_CPU_EXCEPTION, 0, kMfltRebootReason_Nmi);
	reboot_reason_test(K_ERR_KERNEL_OOPS, 0, kMfltRebootReason_Assert);
	reboot_reason_test(K_ERR_KERNEL_PANIC, 0, kMfltRebootReason_Assert);
	reboot_reason_test(K_ERR_STACK_CHK_FAIL, 0, kMfltRebootReason_StackOverflow);

	reboot_reason_test(K_ERR_ARM_BUS_GENERIC, 0, kMfltRebootReason_BusFault);
	reboot_reason_test(K_ERR_ARM_BUS_STACKING, 0, kMfltRebootReason_BusFault);
	reboot_reason_test(K_ERR_ARM_BUS_UNSTACKING, 0, kMfltRebootReason_BusFault);
	reboot_reason_test(K_ERR_ARM_BUS_PRECISE_DATA_BUS, 0, kMfltRebootReason_BusFault);
	reboot_reason_test(K_ERR_ARM_BUS_IMPRECISE_DATA_BUS, 0, kMfltRebootReason_BusFault);
	reboot_reason_test(K_ERR_ARM_BUS_INSTRUCTION_BUS, 0, kMfltRebootReason_BusFault);
	reboot_reason_test(K_ERR_ARM_BUS_FP_LAZY_STATE_PRESERVATION, 0, kMfltRebootReason_BusFault);

	reboot_reason_test(K_ERR_ARM_MEM_GENERIC, 0, kMfltRebootReason_MemFault);
	reboot_reason_test(K_ERR_ARM_MEM_STACKING, 0, kMfltRebootReason_MemFault);
	reboot_reason_test(K_ERR_ARM_MEM_UNSTACKING, 0, kMfltRebootReason_MemFault);
	reboot_reason_test(K_ERR_ARM_MEM_DATA_ACCESS, 0, kMfltRebootReason_MemFault);
	reboot_reason_test(K_ERR_ARM_MEM_INSTRUCTION_ACCESS, 0, kMfltRebootReason_MemFault);
	reboot_reason_test(K_ERR_ARM_MEM_FP_LAZY_STATE_PRESERVATION, 0, kMfltRebootReason_MemFault);

	reboot_reason_test(K_ERR_ARM_USAGE_GENERIC, 0, kMfltRebootReason_UsageFault);
	reboot_reason_test(K_ERR_ARM_USAGE_DIV_0, 0, kMfltRebootReason_UsageFault);
	reboot_reason_test(K_ERR_ARM_USAGE_UNALIGNED_ACCESS, 0, kMfltRebootReason_UsageFault);
	reboot_reason_test(K_ERR_ARM_USAGE_STACK_OVERFLOW, 0, kMfltRebootReason_UsageFault);
	reboot_reason_test(K_ERR_ARM_USAGE_NO_COPROCESSOR, 0, kMfltRebootReason_UsageFault);
	reboot_reason_test(K_ERR_ARM_USAGE_ILLEGAL_EXC_RETURN, 0, kMfltRebootReason_UsageFault);
	reboot_reason_test(K_ERR_ARM_USAGE_ILLEGAL_EPSR, 0, kMfltRebootReason_UsageFault);
	reboot_reason_test(K_ERR_ARM_USAGE_UNDEFINED_INSTRUCTION, 0, kMfltRebootReason_UsageFault);

	reboot_reason_test(K_ERR_ARM_SECURE_GENERIC, 0, kMfltRebootReason_SecurityViolation);
	reboot_reason_test(K_ERR_ARM_SECURE_ENTRY_POINT, 0, kMfltRebootReason_SecurityViolation);
	reboot_reason_test(K_ERR_ARM_SECURE_INTEGRITY_SIGNATURE, 0,
			   kMfltRebootReason_SecurityViolation);
	reboot_reason_test(K_ERR_ARM_SECURE_EXCEPTION_RETURN, 0,
			   kMfltRebootReason_SecurityViolation);
	reboot_reason_test(K_ERR_ARM_SECURE_ATTRIBUTION_UNIT, 0,
			   kMfltRebootReason_SecurityViolation);
	reboot_reason_test(K_ERR_ARM_SECURE_TRANSITION, 0, kMfltRebootReason_SecurityViolation);
	reboot_reason_test(K_ERR_ARM_SECURE_LAZY_STATE_PRESERVATION, 0,
			   kMfltRebootReason_SecurityViolation);
	reboot_reason_test(K_ERR_ARM_SECURE_LAZY_STATE_ERROR, 0,
			   kMfltRebootReason_SecurityViolation);

	reboot_reason_test(INFUSE_REBOOT_RPC, 0, kMfltRebootReason_UserReset);
	reboot_reason_test(INFUSE_REBOOT_CFG_CHANGE, 0, kMfltRebootReason_UserReset);
	reboot_reason_test(INFUSE_REBOOT_DFU, 0, kMfltRebootReason_FirmwareUpdate);
	reboot_reason_test(INFUSE_REBOOT_MCUMGR, 0, kMfltRebootReason_FirmwareUpdate);
	reboot_reason_test(INFUSE_REBOOT_EXTERNAL_TRIGGER, 0, kMfltRebootReason_ButtonReset);
	reboot_reason_test(INFUSE_REBOOT_HW_WATCHDOG, 0, kMfltRebootReason_HardwareWatchdog);
	reboot_reason_test(INFUSE_REBOOT_SW_WATCHDOG, 0, kMfltRebootReason_SoftwareWatchdog);

	reboot_reason_test(INFUSE_REBOOT_UNKNOWN, 0, kMfltRebootReason_Unknown);
	reboot_reason_test(INFUSE_REBOOT_UNKNOWN, RESET_PIN, kMfltRebootReason_PinReset);
	reboot_reason_test(INFUSE_REBOOT_UNKNOWN, RESET_SOFTWARE, kMfltRebootReason_SoftwareReset);
	reboot_reason_test(INFUSE_REBOOT_UNKNOWN, RESET_BROWNOUT, kMfltRebootReason_BrownOutReset);
	reboot_reason_test(INFUSE_REBOOT_UNKNOWN, RESET_POR, kMfltRebootReason_PowerOnReset);
	reboot_reason_test(INFUSE_REBOOT_UNKNOWN, RESET_WATCHDOG,
			   kMfltRebootReason_HardwareWatchdog);
	reboot_reason_test(INFUSE_REBOOT_UNKNOWN, RESET_DEBUG, kMfltRebootReason_DebuggerHalted);
	reboot_reason_test(INFUSE_REBOOT_UNKNOWN, RESET_SECURITY,
			   kMfltRebootReason_SecurityViolation);
	reboot_reason_test(INFUSE_REBOOT_UNKNOWN, RESET_LOW_POWER_WAKE, kMfltRebootReason_LowPower);
	reboot_reason_test(INFUSE_REBOOT_UNKNOWN, RESET_CPU_LOCKUP, kMfltRebootReason_Lockup);
	reboot_reason_test(INFUSE_REBOOT_UNKNOWN, RESET_PARITY, kMfltRebootReason_ParityError);
	reboot_reason_test(INFUSE_REBOOT_UNKNOWN, RESET_PLL, kMfltRebootReason_ClockFailure);
	reboot_reason_test(INFUSE_REBOOT_UNKNOWN, RESET_CLOCK, kMfltRebootReason_ClockFailure);
	reboot_reason_test(INFUSE_REBOOT_UNKNOWN, RESET_HARDWARE, kMfltRebootReason_Hardware);
	reboot_reason_test(INFUSE_REBOOT_UNKNOWN, RESET_USER, kMfltRebootReason_UserReset);
	reboot_reason_test(INFUSE_REBOOT_UNKNOWN, RESET_TEMPERATURE, kMfltRebootReason_Temperature);
}

ZTEST(memfault_integration, test_memfault_platform_time)
{
	struct timeutil_sync_instant reference = {
		.local = 10 * CONFIG_SYS_CLOCK_TICKS_PER_SEC,
		.ref = 100 * INFUSE_EPOCH_TIME_TICKS_PER_SEC,
	};

	sMemfaultCurrentTime out;

	/* No time knowledge, returns false */
	epoch_time_reset();
	zassert_false(memfault_platform_time_get_current(&out));

	/* Some time knowledge, returns true */
	zassert_equal(0, epoch_time_set_reference(TIME_SOURCE_GNSS, &reference));
	zassert_true(memfault_platform_time_get_current(&out));
	zassert_equal(kMemfaultCurrentTimeType_UnixEpochTimeSec, out.type);
	zassert_true(out.info.unix_timestamp_secs > 0);
}

ZTEST(memfault_integration, test_epacket_dump)
{
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots;
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct infuse_reboot_state reboot_state;
	sMfltRebootReason reason;
	ssize_t rc;

	/* KV store should have been initialised and populated with a reboot count */
	rc = KV_STORE_READ(KV_KEY_REBOOTS, &reboots);
	zassert_equal(sizeof(reboots), rc);

	printk("Reboot: %d\n", reboots.count);

	switch (reboots.count) {
	case 1:
		/* Validate chunks are dumped (Cold boot should be small) */
		expect_memfault_chunks(true, 10, 100);
		/* Divide by 0 fault */
		send_fault_command(0, K_ERR_ARM_USAGE_DIV_0);
		k_sleep(K_MSEC(100));
		zassert_unreachable("K_ERR_ARM_USAGE_DIV_0 did not trigger exception");
		break;
	case 2:
		/* Validate chunks are dumped (Fault should have resulted in more data) */
		expect_memfault_chunks(true, 1000, 10000);
		/* Validate previous reboot information */
		rc = infuse_common_boot_last_reboot(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal((enum infuse_reboot_reason)K_ERR_ARM_USAGE_DIV_0,
			      reboot_state.reason);
		/* Trigger annother reboot */
		send_fault_command(0, K_ERR_STACK_CHK_FAIL);
		k_sleep(K_MSEC(100));
		zassert_unreachable("K_ERR_STACK_CHK_FAIL did not trigger exception");
		break;
	case 3:
		/* Run the interface state callback */
		epacket_dummy_set_interface_state(epacket_dummy, true);
		/* Validate chunks are dumped (Fault should have resulted in more data) */
		expect_memfault_chunks(false, 1000, 10000);
		/* Run the interface state callback */
		epacket_dummy_set_interface_state(epacket_dummy, false);
		/* Validate previous reboot information */
		rc = infuse_common_boot_last_reboot(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal((enum infuse_reboot_reason)K_ERR_STACK_CHK_FAIL, reboot_state.reason);
		/* Trigger annother reboot */
		send_fault_command(0, K_ERR_STACK_CHK_FAIL);
		k_sleep(K_MSEC(100));
		zassert_unreachable("K_ERR_STACK_CHK_FAIL did not trigger exception");
		break;
	case 4:
		/* Run the interface state callback */
		epacket_dummy_set_interface_state(epacket_dummy, true);
		/* Validate chunks are dumped (Fault should have resulted in more data) */
		expect_memfault_chunks(false, 1000, 10000);
		/* Run the interface state callback */
		epacket_dummy_set_interface_state(epacket_dummy, false);
		/* Validate previous reboot information */
		rc = infuse_common_boot_last_reboot(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal((enum infuse_reboot_reason)K_ERR_STACK_CHK_FAIL, reboot_state.reason);
		/* Trigger annother reboot */
		send_fault_command(0, K_ERR_STACK_CHK_FAIL);
		k_sleep(K_MSEC(100));
		zassert_unreachable("K_ERR_STACK_CHK_FAIL did not trigger exception");
		break;
	case 5:
		/* Run the interface state callback */
		epacket_dummy_set_interface_state(epacket_dummy, true);

		/* Start dumping all messages */
		rc = infuse_memfault_queue_dump_all(K_NO_WAIT);
		zassert_equal(0, rc);

		/* Simulate the interface going down after the initial size check */
		epacket_dummy_set_max_packet(0);
		epacket_dummy_set_interface_state(epacket_dummy, false);

		/* Wait a little while, then bring it back up */
		k_sleep(K_SECONDS(1));
		epacket_dummy_set_max_packet(CONFIG_EPACKET_PACKET_SIZE_MAX);
		epacket_dummy_set_interface_state(epacket_dummy, true);

		/* Start dumping all messages again */
		rc = infuse_memfault_queue_dump_all(K_NO_WAIT);
		zassert_equal(0, rc);

		/* Validate chunks are dumped (Fault should have resulted in more data) */
		expect_memfault_chunks(false, 1000, 10000);

		/* No more data to dump */
		rc = infuse_memfault_queue_dump_all(K_NO_WAIT);
		zassert_equal(-ENODATA, rc);

		/* Run the interface state callback */
		epacket_dummy_set_interface_state(epacket_dummy, false);
		/* Validate previous reboot information */
		rc = infuse_common_boot_last_reboot(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal((enum infuse_reboot_reason)K_ERR_STACK_CHK_FAIL, reboot_state.reason);

		/* Trigger a reboot that should result in secure fault info being provided */
		infuse_reboot(K_ERR_ARM_SECURE_GENERIC, 0x1234, 0x5678);
		zassert_unreachable("infuse_reboot did not result in reboot ");
		break;
	case 6:
		/* Memfault should know about the secure fault due to our injection */
		rc = memfault_reboot_tracking_get_reboot_reason(&reason);
		zassert_equal(0, rc);
		zassert_equal(kMfltRebootReason_SecurityViolation, reason.reboot_reg_reason);
		/* Try dump with no payload */
		epacket_dummy_set_max_packet(0);
		rc = infuse_memfault_queue_dump_all(K_NO_WAIT);
		zassert_equal(-ENOTCONN, rc);
		/* Reset payload size */
		epacket_dummy_set_max_packet(CONFIG_EPACKET_PACKET_SIZE_MAX);
		/* Secure faults result in a trace event being logged just after boot */
		k_sleep(K_MSEC(2000));
		/* Dump all messages */
		rc = infuse_memfault_queue_dump_all(K_NO_WAIT);
		zassert_equal(0, rc);
		/* Validate chunks are dumped (Reboot info should be small) */
		expect_memfault_chunks(false, 200, 300);
		break;
	default:
		zassert_unreachable("Unexpected reboot count");
		break;
	}
}

ZTEST_SUITE(memfault_integration, NULL, NULL, NULL, NULL, NULL);
