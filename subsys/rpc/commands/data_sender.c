/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/random/random.h>
#include <zephyr/net/buf.h>
#include <zephyr/logging/log.h>

#include <infuse/epacket/packet.h>
#include <infuse/rpc/commands.h>
#include <infuse/rpc/types.h>

#include "../command_runner.h"

LOG_MODULE_DECLARE(rpc_server);

struct net_buf *rpc_command_data_sender(struct net_buf *request)
{
	const struct device *interface;
	struct infuse_rpc_data *data;
	enum epacket_auth auth;
	struct net_buf *data_buf;
	uint32_t request_id;
	uint32_t remaining;
	uint32_t tx_offset = 0;
	uint8_t *payload;
	size_t tail;

	/* Cache data from request and free the buffer.
	 * Scoped in a block to ensure request packet is not used after free.
	 */
	{
		struct epacket_rx_metadata *req_meta = net_buf_user_data(request);
		struct rpc_data_sender_request *req = (void *)request->data;

		request_id = req->header.request_id;
		interface = req_meta->interface;
		auth = req_meta->auth;
		remaining = req->data_header.size;

		rpc_command_runner_request_unref(request);
		request = NULL;
	}
	LOG_DBG("Sending %d bytes", remaining);

	while (remaining > 0) {
		/* Allocate the data packet */
		data_buf = epacket_alloc_tx_for_interface(interface, K_FOREVER);
		epacket_set_tx_metadata(data_buf, auth, 0x00, INFUSE_RPC_DATA, EPACKET_ADDR_ALL);

		/* Allocate header and calculate packets on first iteration */
		data = net_buf_add(data_buf, sizeof(*data));
		tail = MIN(remaining, net_buf_tailroom(data_buf));

		/* Populate INFUSE_RPC_DATA header */
		data->request_id = request_id;
		data->offset = tx_offset;

		/* Populate data payload */
		payload = net_buf_add(data_buf, tail);
		sys_rand_get(payload, tail);

		/* Push payload over interface */
		epacket_queue(interface, data_buf);
		remaining -= tail;
		tx_offset += tail;
	}

	/* Allocate and return response */
	struct rpc_data_sender_response rsp;
	struct net_buf *response = rpc_response_simple_if(interface, 0, &rsp, sizeof(rsp));

	return response;
}
