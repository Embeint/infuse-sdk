/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>
#include <zephyr/random/random.h>

#include <infuse/common_boot.h>
#include <infuse/types.h>
#include <infuse/reboot.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/rpc/types.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/drivers/watchdog.h>

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

static void expect_fault_response(uint32_t request_id, int16_t rc)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_fault_response *response;
	struct net_buf *rsp;

	zassert_not_null(response_queue);

	/* Response was sent */
	rsp = k_fifo_get(response_queue, K_MSEC(100));
	zassert_not_null(rsp);
	response = (void *)(rsp->data + sizeof(struct epacket_dummy_frame));

	/* Parameters match what we expect */
	zassert_equal(request_id, response->header.request_id);
	zassert_equal(rc, response->header.return_code);

	/* Free the response */
	net_buf_unref(rsp);
}

ZTEST(rpc_command_fault, test_does_fault)
{
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots;
	struct infuse_reboot_state reboot_state;
	ssize_t rc;

	/* KV store should have been initialised and populated with a reboot count */
	rc = KV_STORE_READ(KV_KEY_REBOOTS, &reboots);
	zassert_equal(sizeof(reboots), rc);

	switch (reboots.count) {
	case 1:
		/* Stack overflow fault */
		send_fault_command(0, K_ERR_STACK_CHK_FAIL);
		k_sleep(K_MSEC(100));
		zassert_unreachable("K_ERR_STACK_CHK_FAIL did not trigger exception");

		break;
	case 2:
		/* Validate previous reboot information */
		rc = infuse_common_boot_last_reboot(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal((enum infuse_reboot_reason)K_ERR_STACK_CHK_FAIL, reboot_state.reason);
		/* Data access fault */
		send_fault_command(0, K_ERR_ARM_MEM_DATA_ACCESS);
		k_sleep(K_MSEC(100));
		zassert_unreachable("K_ERR_ARM_MEM_DATA_ACCESS did not trigger exception");
		break;
	case 3:
		/* Validate previous reboot information */
		rc = infuse_common_boot_last_reboot(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal((enum infuse_reboot_reason)K_ERR_ARM_MEM_DATA_ACCESS,
			      reboot_state.reason);
		/* Divide by 0 */
		send_fault_command(0, K_ERR_ARM_USAGE_DIV_0);
		k_sleep(K_MSEC(100));
		zassert_unreachable("K_ERR_ARM_USAGE_DIV_0 did not trigger exception");
		break;
	case 4:
		/* Validate previous reboot information */
		rc = infuse_common_boot_last_reboot(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal((enum infuse_reboot_reason)K_ERR_ARM_USAGE_DIV_0,
			      reboot_state.reason);
		/* Unaligned memory access */
		send_fault_command(0, K_ERR_ARM_USAGE_UNDEFINED_INSTRUCTION);
		k_sleep(K_MSEC(100));
		zassert_unreachable(
			"K_ERR_ARM_USAGE_UNDEFINED_INSTRUCTION did not trigger exception");
		break;
	case 5:
		/* Validate previous reboot information */
		rc = infuse_common_boot_last_reboot(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal((enum infuse_reboot_reason)K_ERR_ARM_USAGE_UNDEFINED_INSTRUCTION,
			      reboot_state.reason);
		/* Unaligned memory access */
		send_fault_command(0, K_ERR_ARM_MEM_INSTRUCTION_ACCESS);
		k_sleep(K_MSEC(100));
		zassert_unreachable("K_ERR_ARM_MEM_INSTRUCTION_ACCESS did not trigger exception");
		break;
	case 6:
		/* Validate previous reboot information */
		rc = infuse_common_boot_last_reboot(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal((enum infuse_reboot_reason)K_ERR_ARM_MEM_INSTRUCTION_ACCESS,
			      reboot_state.reason);
		/* ASSERT failure */
		send_fault_command(0, K_ERR_KERNEL_PANIC);
		k_sleep(K_MSEC(100));
		zassert_unreachable("K_ERR_KERNEL_PANIC did not trigger exception");
	case 7:
		/* Validate previous reboot information */
		rc = infuse_common_boot_last_reboot(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal((enum infuse_reboot_reason)K_ERR_KERNEL_PANIC, reboot_state.reason);
		/* Watchdog timeout */
		zassert_equal(0, infuse_watchdog_start());
		send_fault_command(0, INFUSE_REBOOT_HW_WATCHDOG);
		k_sleep(K_MSEC(2100));
		zassert_unreachable("Watchdog did not timeout");
	case 8:
		/* Validate previous reboot information */
		rc = infuse_common_boot_last_reboot(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal(INFUSE_REBOOT_HW_WATCHDOG, reboot_state.reason);
		/* Unknown fault code */
		send_fault_command(0x123456, 255);
		expect_fault_response(0x123456, -EINVAL);
		break;
	default:
		zassert_unreachable("Unexpected reboot count");
		break;
	}
}

ZTEST_SUITE(rpc_command_fault, NULL, NULL, NULL, NULL, NULL);
