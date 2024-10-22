/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/ztest.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/types.h>
#include <infuse/rpc/types.h>
#include <infuse/security.h>
#include <infuse/fs/kv_store.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>

static void send_application_info_command(uint32_t request_id)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_NETWORK,
		.flags = 0x0000,
	};
	struct rpc_application_info_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_APPLICATION_INFO,
			},
	};

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static struct net_buf *expect_application_info_response(uint32_t request_id)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_application_info_response *response;
	struct net_buf *rsp;

	zassert_not_null(response_queue);

	/* Response was sent */
	rsp = net_buf_get(response_queue, K_MSEC(100));
	zassert_not_null(rsp);
	net_buf_pull_mem(rsp, sizeof(struct epacket_dummy_frame));
	response = (void *)rsp->data;

	/* Parameters match what we expect */
	zassert_equal(request_id, response->header.request_id);
	zassert_equal(0, response->header.return_code);

	/* Return the response */
	return rsp;
}

ZTEST(rpc_command_application_info, test_basic)
{
	struct rpc_application_info_response *response;
	struct net_buf *rsp;

	send_application_info_command(3);
	rsp = expect_application_info_response(3);
	response = (void *)rsp->data;
	zassert_equal(0, rsp->len - sizeof(*response));

	zassert_equal(12, response->version.major);
	zassert_equal(1, response->version.minor);
	zassert_equal(5, response->version.revision);
	zassert_not_equal(0, response->version.build_num);

	zassert_equal(CONFIG_INFUSE_APPLICATION_ID, response->application_id);
	zassert_equal(k_uptime_seconds(), response->uptime);
#ifdef CONFIG_KV_STORE
	zassert_equal(1, response->reboots);
#else
	zassert_equal(0, response->reboots);
#endif
#ifdef CONFIG_INFUSE_SECURITY
	zassert_equal(infuse_security_network_key_identifier(), response->network_id);
#else
	zassert_equal(0, response->network_id);
#endif
#ifdef CONFIG_KV_STORE
	zassert_equal(kv_store_reflect_crc(), response->kv_crc);
#else
	zassert_equal(0, response->kv_crc);
#endif

	/* No loggers on this test */
	zassert_equal(0, response->data_blocks_internal);
	zassert_equal(0, response->data_blocks_external);

	net_buf_unref(rsp);

	k_sleep(K_SECONDS(3));

	send_application_info_command(4);
	rsp = expect_application_info_response(4);
	response = (void *)rsp->data;
	zassert_equal(0, rsp->len - sizeof(*response));

	zassert_equal(k_uptime_seconds(), response->uptime);
	net_buf_unref(rsp);
}

ZTEST_SUITE(rpc_command_application_info, NULL, NULL, NULL, NULL, NULL);
