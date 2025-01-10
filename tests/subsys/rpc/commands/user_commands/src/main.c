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
#include <infuse/rpc/types.h>
#include <infuse/rpc/commands.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/time/epoch.h>

static int custom_runner_calls;

enum {
	/** Arbitrary user command */
	RPC_ID_USER_COMMAND = RPC_BUILTIN_END + 15,
};

struct rpc_user_command_request {
	struct infuse_rpc_req_header header;
	uint32_t parameter;
} __packed;

struct rpc_user_command_response {
	struct infuse_rpc_rsp_header header;
	uint32_t response;
} __packed;

struct net_buf *rpc_user_command_impl(struct net_buf *request)
{
	struct rpc_user_command_request *req = (void *)request->data;
	struct rpc_user_command_response rsp = {
		.response = req->parameter + 1,
	};

	return rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));
}

int infuse_rpc_server_user_command_runner(uint16_t command_id, enum epacket_auth auth,
					  struct net_buf *request, struct net_buf **response)
{
	int16_t rc = -EACCES;

	custom_runner_calls += 1;

	switch (command_id) {
	case RPC_ID_USER_COMMAND:
		if (auth >= EPACKET_AUTH_DEVICE) {
			*response = rpc_user_command_impl(request);
		}
		break;
	default:
		rc = -ENOTSUP;
	}

	if (*response != NULL) {
		/* Response was allocated, command was handled */
		rc = 0;
	}

	return rc;
}

static void send_user_command(uint16_t command_id, uint32_t request_id, enum epacket_auth auth,
			      uint32_t parameter)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = auth,
		.flags = 0x0000,
	};
	struct rpc_user_command_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = command_id,
			},
		.parameter = parameter,
	};

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static struct rpc_user_command_response expect_user_command_response(uint32_t request_id,
								     int16_t rc)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_user_command_response *response, out;
	struct net_buf *rsp;

	zassert_not_null(response_queue);

	/* Response was sent */
	rsp = net_buf_get(response_queue, K_MSEC(100));
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

ZTEST(rpc_command_user, test_user_command)
{
	struct rpc_user_command_response response;

	zassert_equal(0, custom_runner_calls);

	/* Send the user defined command */
	send_user_command(RPC_ID_USER_COMMAND, 10, EPACKET_AUTH_DEVICE, 20);
	response = expect_user_command_response(10, 0);
	zassert_equal(20 + 1, response.response);
	zassert_equal(1, custom_runner_calls);

	/* Send user defined command with insufficient auth */
	send_user_command(RPC_ID_USER_COMMAND, 11, EPACKET_AUTH_NETWORK, 20);
	response = expect_user_command_response(11, -EACCES);
	zassert_equal(2, custom_runner_calls);

	/* Send unknown user defined command  */
	send_user_command(RPC_ID_USER_COMMAND + 1, 12, EPACKET_AUTH_NETWORK, 20);
	response = expect_user_command_response(12, -ENOTSUP);
	zassert_equal(3, custom_runner_calls);

	/* Send unknown user defined command not in user defined range */
	send_user_command(RPC_BUILTIN_END, 13, EPACKET_AUTH_NETWORK, 20);
	response = expect_user_command_response(13, -ENOTSUP);
	zassert_equal(3, custom_runner_calls);
}

ZTEST_SUITE(rpc_command_user, NULL, NULL, NULL, NULL, NULL);
