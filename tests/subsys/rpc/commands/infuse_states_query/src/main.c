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

static void send_infuse_states_query_command(uint32_t request_id, uint8_t offset)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_infuse_states_query_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_INFUSE_STATES_QUERY,
			},
		.offset = offset,
	};

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static struct net_buf *expect_infuse_states_query_response(uint32_t request_id)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_infuse_states_query_response *response;
	struct net_buf *rsp;

	zassert_not_null(response_queue);

	/* Response was sent */
	rsp = k_fifo_get(response_queue, K_MSEC(100));
	zassert_not_null(rsp);
	net_buf_pull_mem(rsp, sizeof(struct epacket_dummy_frame));
	response = (void *)rsp->data;

	/* Parameters match what we expect */
	zassert_equal(request_id, response->header.request_id);
	zassert_equal(0, response->header.return_code);

	/* Return the response */
	return rsp;
}

ZTEST(rpc_command_infuse_states_query, test_basic)
{
	INFUSE_STATES_ARRAY(current);
	struct rpc_infuse_states_query_response *response;
	struct net_buf *rsp;
	size_t trailing;

	/* Initial state (no set states) */
	send_infuse_states_query_command(3, 0);
	rsp = expect_infuse_states_query_response(3);
	response = (void *)rsp->data;
	trailing = rsp->len - sizeof(*response);
	zassert_equal(0, trailing);
	zassert_equal(0, response->remaining);
	net_buf_unref(rsp);

	/* Set a single state */
	infuse_state_set(INFUSE_STATE_TIME_KNOWN);
	send_infuse_states_query_command(4, 0);
	rsp = expect_infuse_states_query_response(4);
	response = (void *)rsp->data;
	trailing = rsp->len - sizeof(*response);
	zassert_equal(1 * sizeof(struct rpc_struct_infuse_state), trailing);
	zassert_equal(0, response->remaining);
	zassert_equal(INFUSE_STATE_TIME_KNOWN, response->states[0].state);
	zassert_equal(0, response->states[0].timeout);
	net_buf_unref(rsp);

	/* Set a second state with timeout */
	infuse_state_set_timeout(INFUSE_STATE_DEVICE_STATIONARY, 10);
	send_infuse_states_query_command(5, 0);
	rsp = expect_infuse_states_query_response(5);
	response = (void *)rsp->data;
	trailing = rsp->len - sizeof(*response);
	zassert_equal(2 * sizeof(struct rpc_struct_infuse_state), trailing);
	zassert_equal(0, response->remaining);
	zassert_equal(INFUSE_STATE_TIME_KNOWN, response->states[0].state);
	zassert_equal(0, response->states[0].timeout);
	zassert_equal(INFUSE_STATE_DEVICE_STATIONARY, response->states[1].state);
	zassert_equal(10, response->states[1].timeout);
	net_buf_unref(rsp);

	/* Iterate timeouts */
	infuse_states_snapshot(current);
	infuse_states_tick(current);

	/* Timeout should have reduced */
	send_infuse_states_query_command(6, 0);
	rsp = expect_infuse_states_query_response(6);
	response = (void *)rsp->data;
	trailing = rsp->len - sizeof(*response);
	zassert_equal(2 * sizeof(struct rpc_struct_infuse_state), trailing);
	zassert_equal(0, response->remaining);
	zassert_equal(INFUSE_STATE_TIME_KNOWN, response->states[0].state);
	zassert_equal(0, response->states[0].timeout);
	zassert_equal(INFUSE_STATE_DEVICE_STATIONARY, response->states[1].state);
	zassert_equal(9, response->states[1].timeout);
	net_buf_unref(rsp);

	/* Set a bunch more states */
	for (int i = 0; i < 10; i++) {
		infuse_state_set(INFUSE_STATES_APP_START + i);
	}
	send_infuse_states_query_command(7, 0);
	rsp = expect_infuse_states_query_response(7);
	response = (void *)rsp->data;
	trailing = rsp->len - sizeof(*response);
	zassert_equal(12 * sizeof(struct rpc_struct_infuse_state), trailing);
	zassert_equal(0, response->remaining);
	zassert_equal(INFUSE_STATE_TIME_KNOWN, response->states[0].state);
	zassert_equal(0, response->states[0].timeout);
	zassert_equal(INFUSE_STATE_DEVICE_STATIONARY, response->states[1].state);
	zassert_equal(9, response->states[1].timeout);
	for (int i = 0; i < 10; i++) {
		zassert_equal(INFUSE_STATES_APP_START + i, response->states[2 + i].state);
		zassert_equal(0, response->states[2 + i].timeout);
	}
	net_buf_unref(rsp);

	/* Skip first 2 states */
	send_infuse_states_query_command(7, 2);
	rsp = expect_infuse_states_query_response(7);
	response = (void *)rsp->data;
	trailing = rsp->len - sizeof(*response);
	zassert_equal(10 * sizeof(struct rpc_struct_infuse_state), trailing);
	zassert_equal(0, response->remaining);
	for (int i = 0; i < 10; i++) {
		zassert_equal(INFUSE_STATES_APP_START + i, response->states[i].state);
		zassert_equal(0, response->states[i].timeout);
	}
	net_buf_unref(rsp);

	/* Reduce packet size so not all states can fit */
	epacket_dummy_set_max_packet(30);

	send_infuse_states_query_command(7, 0);
	rsp = expect_infuse_states_query_response(7);
	response = (void *)rsp->data;
	trailing = rsp->len - sizeof(*response);
	zassert_equal(5 * sizeof(struct rpc_struct_infuse_state), trailing);
	zassert_equal(12 - 5, response->remaining);
	zassert_equal(INFUSE_STATE_TIME_KNOWN, response->states[0].state);
	zassert_equal(0, response->states[0].timeout);
	zassert_equal(INFUSE_STATE_DEVICE_STATIONARY, response->states[1].state);
	zassert_equal(9, response->states[1].timeout);
	for (int i = 0; i < (5 - 2); i++) {
		zassert_equal(INFUSE_STATES_APP_START + i, response->states[2 + i].state);
		zassert_equal(0, response->states[2 + i].timeout);
	}
	net_buf_unref(rsp);
}

ZTEST_SUITE(rpc_command_infuse_states_query, NULL, NULL, NULL, NULL, NULL);
