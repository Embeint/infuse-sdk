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
#include <infuse/rpc/types.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/time/epoch.h>

static void send_time_set_command(uint32_t request_id, uint64_t time)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_time_set_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_TIME_SET,
			},
		.epoch_time = time,
	};

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static void expect_time_set_response(uint32_t request_id, int16_t rc)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_time_set_response *response;
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

static void send_time_get_command(uint32_t request_id)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_time_get_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_TIME_GET,
			},
	};

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static struct rpc_time_get_response expect_time_get_response(uint32_t request_id, int16_t rc)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_time_get_response *response, out;
	struct net_buf *rsp;

	zassert_not_null(response_queue);

	/* Response was sent */
	rsp = k_fifo_get(response_queue, K_MSEC(100));
	zassert_not_null(rsp);
	response = (void *)(rsp->data + sizeof(struct epacket_dummy_frame));

	/* Parameters match what we expect */
	zassert_equal(request_id, response->header.request_id);
	zassert_equal(rc, response->header.return_code);

	/* Store response */
	out = *response;

	/* Free the response */
	net_buf_unref(rsp);

	return out;
}

ZTEST(rpc_command_time, test_time_get_set)
{
	extern struct timeutil_sync_state infuse_sync_state;
	struct rpc_time_get_response time_get;
	uint64_t test_time = 0x123456789ABCD;

	zassert_equal(TIME_SOURCE_NONE, epoch_time_get_source());
	zassert_equal(UINT32_MAX, epoch_time_reference_age());

	/* Send the time get RPC */
	send_time_get_command(6);
	time_get = expect_time_get_response(6, 0);
	zassert_equal(TIME_SOURCE_NONE, time_get.time_source);
	zassert_equal(UINT32_MAX, time_get.sync_age);

	/* Send the time set RPC */
	send_time_set_command(9, test_time);
	expect_time_set_response(9, 0);

	/* Validate parameters set */
	zassert_equal(TIME_SOURCE_RPC, epoch_time_get_source());
	zassert_equal(0, epoch_time_reference_age());
	zassert_equal(test_time, infuse_sync_state.base.ref);

	k_sleep(K_MSEC(100));

	/* Send the time get RPC */
	send_time_get_command(100);
	time_get = expect_time_get_response(100, 0);
	zassert_equal(TIME_SOURCE_RPC, time_get.time_source);
	zassert_equal(0, time_get.sync_age);
	zassert_true(time_get.epoch_time > test_time);
	zassert_true(time_get.epoch_time <= test_time + 10000);

	/* Wait a bit and query again */
	k_sleep(K_SECONDS(2));
	send_time_get_command(101);
	time_get = expect_time_get_response(101, 0);
	zassert_equal(TIME_SOURCE_RPC, time_get.time_source);
	zassert_equal(2, time_get.sync_age);
}

ZTEST_SUITE(rpc_command_time, NULL, NULL, NULL, NULL, NULL);
