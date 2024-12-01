/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/net/buf.h>
#include <zephyr/sys/crc.h>

#include <infuse/data_logger/logger.h>
#include <infuse/epacket/packet.h>
#include <infuse/rpc/types.h>

#include "../command_runner.h"
#include "../server.h"

struct net_buf *rpc_command_data_logger_read(struct net_buf *request)
{
	struct epacket_rx_metadata *req_meta = net_buf_user_data(request);
	const struct device *interface = req_meta->interface;
	struct rpc_data_logger_read_request *req = (void *)request->data;
	struct rpc_data_logger_read_response rsp = {0};
	struct data_logger_state state;
	const struct device *logger;
	struct net_buf *data_buf = NULL;
	struct infuse_rpc_data *data = NULL;
	uint8_t *work_mem;
	size_t work_mem_size;
	int rc = 0;

	switch (req->logger) {
#ifdef CONFIG_DATA_LOGGER_FLASH_MAP
	case RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD:
		logger = DEVICE_DT_GET_ONE(embeint_data_logger_flash_map);
		break;
#endif /* CONFIG_DATA_LOGGER_FLASH_MAP */
#ifdef CONFIG_DATA_LOGGER_EXFAT
	case RPC_ENUM_DATA_LOGGER_FLASH_REMOVABLE:
		logger = DEVICE_DT_GET_ONE(embeint_data_logger_exfat);
		break;
#endif /* CONFIG_DATA_LOGGER_EXFAT */
	default:
		rc = -ENODEV;
		goto end;
	}

	/* Ensure device initialised properly */
	if (!device_is_ready(logger)) {
		rc = -EBADF;
		goto end;
	}

	data_logger_get_state(logger, &state);
	work_mem = rpc_server_command_working_mem(&work_mem_size);
	if (work_mem_size < state.block_size) {
		rc = -ENOMEM;
		goto end;
	}

	/* Ensure requested data is in range */
	if ((req->start_block < state.earliest_block) || (req->last_block >= state.current_block) ||
	    (req->last_block < req->start_block)) {
		rc = -EINVAL;
		goto end;
	}

	union epacket_interface_address addr = req_meta->interface_address;
	enum epacket_auth auth = req_meta->auth;
	uint32_t block_num = req->start_block;
	uint32_t blocks_remaining = req->last_block - req->start_block + 1;
	uint32_t request_id = req->header.request_id;
	uint16_t block_remaining, block_offset;
	size_t tail;

	/* Free command as we no longer need it and this command can take a while */
	rpc_command_runner_request_unref(request);
	req = NULL;
	req_meta = NULL;

	while (blocks_remaining--) {
		/* Read the complete block */
		rc = data_logger_block_read(logger, block_num, 0, work_mem, state.block_size);
		if (rc < 0) {
			break;
		}
		block_remaining = state.block_size;
		block_offset = 0;
		block_num += 1;

		/* Push all block data into messages */
		while (block_remaining) {
			if (data_buf == NULL) {
				/* Allocate new data message */
				data_buf = epacket_alloc_tx_for_interface(interface, K_FOREVER);
				epacket_set_tx_metadata(data_buf, auth, 0x00, INFUSE_RPC_DATA,
							addr);

				/* Allocate header and calculate packets on first iteration */
				data = net_buf_add(data_buf, sizeof(*data));
				data->request_id = request_id;
				data->offset = rsp.sent_len;
			}

			/* Add as much payload as we can to buffer (aligned to 4 byte chunks) */
			tail = MIN(block_remaining, net_buf_tailroom(data_buf));
			tail = ROUND_DOWN(tail, 4);
			net_buf_add_mem(data_buf, work_mem + block_offset, tail);
			block_remaining -= tail;
			block_offset += tail;

			if (net_buf_tailroom(data_buf) < sizeof(uint32_t)) {
				/* Update sent data CRC (payload only, not the header )*/
				rsp.sent_crc = crc32_ieee_update(rsp.sent_crc,
								 data_buf->data + sizeof(*data),
								 data_buf->len - sizeof(*data));
				rsp.sent_len += data_buf->len - sizeof(*data);

				/* Send full buffer */
				epacket_queue(interface, data_buf);
				data_buf = NULL;
			}
		}
	}
	/* Flush final buffers */
	if ((rc == 0) && (data_buf != NULL)) {
		/* Update sent data CRC (payload only, not the header )*/
		rsp.sent_crc = crc32_ieee_update(rsp.sent_crc, data_buf->data + sizeof(*data),
						 data_buf->len - sizeof(*data));
		rsp.sent_len += data_buf->len - sizeof(*data);

		/* Send full buffer */
		epacket_queue(interface, data_buf);
	}

end:
	return rpc_response_simple_if(interface, rc, &rsp, sizeof(rsp));
}
