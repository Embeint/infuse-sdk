/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net_buf.h>
#include <zephyr/sys/crc.h>

#include <infuse/data_logger/logger.h>
#include <infuse/epacket/packet.h>
#include <infuse/rpc/commands.h>
#include <infuse/rpc/command_runner.h>
#include <infuse/rpc/types.h>

/* Arbitrary memory reads MUST require device authentication */
BUILD_ASSERT(CONFIG_INFUSE_RPC_COMMAND_MEM_READ_REQUIRED_AUTH == 2);

struct net_buf *rpc_command_mem_read(struct net_buf *request)
{
	struct epacket_rx_metadata *req_meta = net_buf_user_data(request);
	const struct device *interface = req_meta->interface;
	struct rpc_mem_read_request *req = (void *)request->data;
	struct rpc_mem_read_response rsp = {0};
	struct net_buf *data_buf = NULL;
	struct infuse_rpc_data *data = NULL;
	union epacket_interface_address addr = req_meta->interface_address;
	enum epacket_auth auth = req_meta->auth;
	uint8_t *current_address = (void *)req->address;
	uint32_t bytes_remaining = req->data_header.size;
	uint32_t request_id = req->header.request_id;
	k_ticks_t limit_tx = k_uptime_ticks();
	size_t tail = 0;

	/* Free command as we no longer need it and this command can take a while */
	rpc_command_runner_request_unref(request);
	req = NULL;
	req_meta = NULL;

	while (bytes_remaining) {
		/* Feed watchdog as this can be a long running process if the data size is high */
		rpc_server_watchdog_feed();

		/* Respect any rate-limiting requests from the receiving device */
		epacket_rate_limit_tx(&limit_tx, tail);

		/* Allocate new data message */
		data_buf = epacket_alloc_tx_for_interface(interface, K_FOREVER);
		if (net_buf_tailroom(data_buf) == 0) {
			/* Backend connection has been lost */
			net_buf_unref(data_buf);
			break;
		}
		epacket_set_tx_metadata(data_buf, auth, 0x00, INFUSE_RPC_DATA, addr);

		/* Allocate header and calculate packets on first iteration */
		data = net_buf_add(data_buf, sizeof(*data));
		data->request_id = request_id;
		data->offset = rsp.sent_len;

		/* Add as much payload as we can to buffer (aligned to 4 byte chunks) */
		tail = MIN(bytes_remaining, net_buf_tailroom(data_buf));
		if (tail > 4) {
			tail = ROUND_DOWN(tail, 4);
		}
		net_buf_add_mem(data_buf, current_address, tail);

		/* Update sent data CRC (payload only, not the header) */
		rsp.sent_crc = crc32_ieee_update(rsp.sent_crc, current_address, tail);
		rsp.sent_len += tail;

		/* Send full buffer */
		epacket_queue(interface, data_buf);

		/* Next iteration */
		current_address += tail;
		bytes_remaining -= tail;
	}

	return rpc_response_simple_if(interface, 0, &rsp, sizeof(rsp));
}
