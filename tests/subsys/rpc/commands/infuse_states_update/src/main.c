/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>

#include <infuse/states.h>
#include <infuse/types.h>
#include <infuse/rpc/types.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>

static void send_infuse_states_update_command(uint32_t request_id,
					      struct rpc_struct_infuse_state *states, uint8_t num)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_infuse_states_update_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_INFUSE_STATES_UPDATE,
			},
		.num = num,
	};

	/* Push command at RPC server */
	epacket_dummy_receive_extra(epacket_dummy, &header, &params, sizeof(params), states,
				    num * sizeof(*states));
}

static struct net_buf *expect_infuse_states_update_response(uint32_t request_id, int rc)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_infuse_states_update_response *response;
	struct net_buf *rsp;

	zassert_not_null(response_queue);

	/* Response was sent */
	rsp = k_fifo_get(response_queue, K_MSEC(100));
	zassert_not_null(rsp);
	net_buf_pull_mem(rsp, sizeof(struct epacket_dummy_frame));
	response = (void *)rsp->data;

	/* Parameters match what we expect */
	zassert_equal(request_id, response->header.request_id);
	zassert_equal(rc, response->header.return_code);

	/* Return the response */
	return rsp;
}

ZTEST(rpc_command_infuse_states_update, test_invalid)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_infuse_states_update_request params = {
		.header =
			{
				.request_id = 1,
				.command_id = RPC_ID_INFUSE_STATES_UPDATE,
			},
		.num = 3,
	};
	struct rpc_struct_infuse_state states[3] = {0};
	struct net_buf *rsp;

	/* Push command with a bad payload */
	epacket_dummy_receive_extra(epacket_dummy, &header, &params, sizeof(params), (void *)states,
				    5);

	rsp = expect_infuse_states_update_response(1, -EINVAL);
	net_buf_unref(rsp);
}

ZTEST(rpc_command_infuse_states_update, test_basic)
{
	struct rpc_struct_infuse_state states[3];
	struct net_buf *rsp;

	/* Set a single state */
	states[0].state = INFUSE_STATE_TIME_KNOWN;
	states[0].timeout = 0;

	send_infuse_states_update_command(4, states, 1);
	rsp = expect_infuse_states_update_response(4, 0);
	net_buf_unref(rsp);

	zassert_true(infuse_state_get(INFUSE_STATE_TIME_KNOWN));
	zassert_equal(0, infuse_state_get_timeout(INFUSE_STATE_TIME_KNOWN));

	/* State with timeout */
	states[0].state = INFUSE_STATE_TIME_KNOWN;
	states[0].timeout = 10;
	states[1].state = INFUSE_STATE_DEVICE_STATIONARY;
	states[1].timeout = 0;

	send_infuse_states_update_command(5, states, 2);
	rsp = expect_infuse_states_update_response(5, 0);
	net_buf_unref(rsp);

	zassert_true(infuse_state_get(INFUSE_STATE_TIME_KNOWN));
	zassert_equal(10, infuse_state_get_timeout(INFUSE_STATE_TIME_KNOWN));
	zassert_true(infuse_state_get(INFUSE_STATE_DEVICE_STATIONARY));
	zassert_equal(0, infuse_state_get_timeout(INFUSE_STATE_DEVICE_STATIONARY));

	/* State clear */
	states[0].state = INFUSE_STATE_TIME_KNOWN;
	states[0].timeout = 0;
	states[1].state = INFUSE_STATE_DEVICE_STATIONARY;
	states[1].timeout = UINT16_MAX;
	states[2].state = INFUSE_STATES_APP_START;
	states[2].timeout = 2;

	send_infuse_states_update_command(5, states, 3);
	rsp = expect_infuse_states_update_response(5, 0);
	net_buf_unref(rsp);

	zassert_true(infuse_state_get(INFUSE_STATE_TIME_KNOWN));
	zassert_equal(0, infuse_state_get_timeout(INFUSE_STATE_TIME_KNOWN));
	zassert_false(infuse_state_get(INFUSE_STATE_DEVICE_STATIONARY));
	zassert_true(infuse_state_get(INFUSE_STATES_APP_START));
	zassert_equal(2, infuse_state_get_timeout(INFUSE_STATES_APP_START));
}

ZTEST_SUITE(rpc_command_infuse_states_update, NULL, NULL, NULL, NULL, NULL);
