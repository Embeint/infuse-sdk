/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>

#include <infuse/rpc/commands.h>
#include <infuse/rpc/command_runner.h>
#include <infuse/rpc/types.h>
#include <infuse/epacket/packet.h>

LOG_MODULE_DECLARE(rpc_server);

struct net_buf *rpc_command_data_receiver(struct net_buf *request)
{
	struct infuse_rpc_data *data;
	const struct device *interface;
	union epacket_interface_address from;
	uint32_t request_id, remaining, expected;
	struct net_buf *data_buf;
	enum epacket_auth auth;
	uint32_t data_offset;
	uint32_t received = 0;
	uint32_t expected_offset = 0;
	uint8_t ack_period;
	uint32_t crc = 0;
	size_t var_len;
	int rc = 0;

	/* Cache data from request and free the buffer.
	 * Scoped in a block to ensure request packet is not used after free.
	 */
	{
		struct epacket_rx_metadata *req_meta = net_buf_user_data(request);
		struct rpc_data_receiver_request *req = (void *)request->data;

		request_id = req->header.request_id;
		interface = req_meta->interface;
		from = req_meta->interface_address;
		auth = req_meta->auth;
		expected = req->data_header.size;
		remaining = req->data_header.size;
		ack_period = req->data_header.rx_ack_period;

		rpc_command_runner_request_unref(request);
		request = NULL;
	}
	LOG_DBG("Receiving %d bytes", remaining);

	/* Initial ACK to signal readiness */
	rpc_server_ack_data_ready(interface, from, request_id);

	while (remaining > 0) {
		data_buf = rpc_server_pull_data(request_id, expected_offset, &rc, K_MSEC(500));
		if (data_buf == NULL) {
			goto end;
		}
		var_len = RPC_DATA_VAR_LEN(data_buf);
		if (var_len > remaining) {
			LOG_WRN("Received too much data %d/%d", var_len, remaining);
			rc = -EINVAL;
			goto end;
		}
		data = (void *)data_buf->data;
		crc = crc32_ieee_update(crc, data->payload, var_len);
		data_offset = data->offset;
		expected_offset = data_offset + var_len;
		remaining = expected - expected_offset;
		received += var_len;
		net_buf_unref(data_buf);

		/* Handle any acknowledgements required */
		if (remaining > 0) {
			rpc_server_ack_data(interface, from, request_id, data_offset, ack_period);
		}
	}

end:
	/* Allocate and return response (simulate early response) */
	struct rpc_data_receiver_response rsp = {
		.recv_len = received,
		.recv_crc = crc,
	};
	struct net_buf *response = rpc_response_simple_if(interface, rc, &rsp, sizeof(rsp));

	rpc_command_runner_early_response(interface, from, auth, request_id, RPC_ID_DATA_RECEIVER,
					  response);
	k_sleep(K_MSEC(100));
	return NULL;
}
