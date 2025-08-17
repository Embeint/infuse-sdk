/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
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

static void send_reboot_command(uint32_t request_id, uint32_t delay_ms)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_reboot_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_REBOOT,
			},
		.delay_ms = delay_ms,
	};

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static void expect_reboot_response(uint32_t request_id, uint32_t delay_ms)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_reboot_response *response;
	struct net_buf *rsp;

	zassert_not_null(response_queue);

	/* Response was sent */
	rsp = k_fifo_get(response_queue, K_MSEC(100));
	zassert_not_null(rsp);
	response = (void *)(rsp->data + sizeof(struct epacket_dummy_frame));

	/* Parameters match what we expect */
	zassert_equal(request_id, response->header.request_id);
	zassert_equal(0, response->header.return_code);
	zassert_equal(delay_ms, response->delay_ms);

	/* Free the response */
	net_buf_unref(rsp);
}

/* Just to get the address */
extern struct net_buf *rpc_command_reboot(struct net_buf *request);

ZTEST(rpc_command_reboot, test_does_reboot)
{
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots;
	struct infuse_reboot_state reboot_state;
	ssize_t rc;

	/* KV store should have been initialised and populated with a reboot count */
	rc = KV_STORE_READ(KV_KEY_REBOOTS, &reboots);
	zassert_equal(sizeof(reboots), rc);

	switch (reboots.count) {
	case 1:
		/* Send command with default timeout */
		send_reboot_command(1, 0);
		/* Validate the response */
		expect_reboot_response(1, 2000);
		/* Wait for the reboot */
		k_sleep(K_SECONDS(3));
		zassert_unreachable("Reboot RPC did not trigger command");
		break;
	case 2:
		/* Validate previous reboot information */
		rc = infuse_common_boot_last_reboot(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal(INFUSE_REBOOT_RPC, reboot_state.reason);
		zassert_equal(2, reboot_state.uptime);
		zassert_equal((uintptr_t)rpc_command_reboot, reboot_state.info.generic.info1);
		/* Wait to send next command */
		k_sleep(K_SECONDS(1));
		/* Trigger another reboot with explicit delay */
		send_reboot_command(1000, 3500);
		/* Validate the response */
		expect_reboot_response(1000, 3500);
		/* Wait for the reboot */
		k_sleep(K_SECONDS(4));
		zassert_unreachable("Reboot RPC did not trigger command");
		break;
	case 3:
		/* Validate previous reboot information */
		rc = infuse_common_boot_last_reboot(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal(INFUSE_REBOOT_RPC, reboot_state.reason);
		zassert_equal(4, reboot_state.uptime);
		zassert_equal((uintptr_t)rpc_command_reboot, reboot_state.info.generic.info1);
		/* Test done */
		break;
	default:
		zassert_unreachable("Unexpected reboot count");
		break;
	}
}

ZTEST_SUITE(rpc_command_reboot, NULL, NULL, NULL, NULL, NULL);
