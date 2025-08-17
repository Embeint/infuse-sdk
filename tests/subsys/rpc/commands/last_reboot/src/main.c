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
#include <infuse/types.h>
#include <infuse/time/epoch.h>
#include <infuse/reboot.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/rpc/types.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>

static void null_dereference(void)
{
	epoch_time_set_reference(TIME_SOURCE_NONE, NULL);
}

static void send_last_reboot_command(uint32_t request_id)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_last_reboot_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_LAST_REBOOT,
			},
	};

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static struct net_buf *expect_last_reboot_response(uint32_t request_id)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_reboot_response *response;
	struct net_buf *rsp;

	zassert_not_null(response_queue);

	/* Response was sent */
	rsp = k_fifo_get(response_queue, K_MSEC(100));
	zassert_not_null(rsp);
	net_buf_pull_mem(rsp, sizeof(struct epacket_dummy_frame));
	response = (void *)(rsp->data);

	/* Parameters match what we expect */
	zassert_equal(request_id, response->header.request_id);
	zassert_equal(RPC_ID_LAST_REBOOT, response->header.command_id);
	zassert_equal(0, response->header.return_code);

	/* Return the response */
	return rsp;
}

ZTEST(rpc_command_last_reboot, test_reboot_query)
{
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots;
	struct rpc_last_reboot_response *response;
	struct net_buf *rsp;
	int esf_values;
	ssize_t rc;

	/* KV store should have been initialised and populated with a reboot count */
	rc = KV_STORE_READ(KV_KEY_REBOOTS, &reboots);
	zassert_equal(sizeof(reboots), rc);

	switch (reboots.count) {
	case 1:
		/* Query initial reboot info */
		send_last_reboot_command(1);
		/* Validate the response */
		rsp = expect_last_reboot_response(1);
		response = (void *)rsp->data;
		esf_values = (rsp->len - sizeof(*response)) / sizeof(response->esf[0]);
		zassert_equal(INFUSE_REBOOT_UNKNOWN, response->reason);
		zassert_equal(TIME_SOURCE_NONE, response->epoch_time_source);
		zassert_equal(0, response->epoch_time);
		zassert_equal(0, response->param_1);
		zassert_equal(0, response->param_2);
		zassert_equal(0, esf_values);
		net_buf_unref(rsp);

		struct timeutil_sync_instant reference = {
			.local = 0,
			.ref = 50000000000000,
		};

		zassert_equal(0, epoch_time_set_reference(TIME_SOURCE_GNSS, &reference));

		/* Trigger a reboot with known params */
		infuse_reboot(INFUSE_REBOOT_DFU, 0x1234, 0x98765432);
		zassert_unreachable("Test did not reboot");
		break;
	case 2:
		/* Validate previous initial reboot info */
		send_last_reboot_command(333);
		/* Validate the response */
		rsp = expect_last_reboot_response(333);
		response = (void *)rsp->data;
		esf_values = (rsp->len - sizeof(*response)) / sizeof(response->esf[0]);
		zassert_equal(INFUSE_REBOOT_DFU, response->reason);
		zassert_equal(TIME_SOURCE_GNSS, response->epoch_time_source);
		zassert_not_equal(0, response->epoch_time);
		zassert_equal(0x1234, response->param_1);
		zassert_equal(0x98765432, response->param_2);
		zassert_equal(0, esf_values);
		net_buf_unref(rsp);

		/* Trigger a fault */
		null_dereference();
		zassert_unreachable("Test did not reboot");
		break;
	case 3:
		/* Validate previous initial reboot info */
		send_last_reboot_command(444);
		rsp = expect_last_reboot_response(444);
		response = (void *)rsp->data;
		esf_values = (rsp->len - sizeof(*response)) / sizeof(response->esf[0]);
		zassert_equal(K_ERR_CPU_EXCEPTION, response->reason);
		zassert_equal(TIME_SOURCE_GNSS | TIME_SOURCE_RECOVERED,
			      response->epoch_time_source);
		zassert_not_equal(0, response->epoch_time);
		/* Expect a full exception stack frame to be part of the response */
		zassert_equal(sizeof(struct arch_esf) / sizeof(uint32_t), esf_values);
		net_buf_unref(rsp);
		break;
	default:
		zassert_unreachable("Unexpected reboot count");
		break;
	}
}

ZTEST_SUITE(rpc_command_last_reboot, NULL, NULL, NULL, NULL, NULL);
