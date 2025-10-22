/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net_buf.h>
#include <zephyr/sys/crc.h>
#include <zephyr/logging/log.h>

#include <infuse/data_logger/logger.h>
#include <infuse/epacket/packet.h>
#include <infuse/rpc/commands.h>
#include <infuse/rpc/command_runner.h>
#include <infuse/rpc/types.h>

LOG_MODULE_DECLARE(rpc_server, CONFIG_INFUSE_RPC_LOG_LEVEL);

struct common_state {
	const struct device *interface;
	const struct device *logger;
	struct data_logger_state logger_state;
	union epacket_interface_address addr;
	enum epacket_auth auth;
	uint32_t request_id;
	uint32_t block_num;
	uint32_t blocks_remaining;
	uint32_t sent_len;
	uint32_t sent_crc;
};

static int do_read(struct common_state *state)
{
	struct net_buf *data_buf = NULL;
	struct infuse_rpc_data *data = NULL;
	uint16_t block_remaining, block_offset;
	k_ticks_t limit_tx = k_uptime_ticks();
	uint16_t tx_count = 0;
	size_t work_mem_size;
	uint8_t *work_mem;
	size_t tail;
	int rc = 0;

	work_mem = rpc_server_command_working_mem(&work_mem_size);
	if (work_mem_size < state->logger_state.block_size) {
		return -ENOMEM;
	}

	LOG_INF("Reading blocks %d-%d from %s", state->block_num,
		state->block_num + state->blocks_remaining - 1, state->logger->name);

	while (state->blocks_remaining--) {
		/* Feed watchdog as this can be a long running process if the block count is high */
		rpc_server_watchdog_feed();

		/* Read the complete block */
		rc = data_logger_block_read(state->logger, state->block_num, 0, work_mem,
					    state->logger_state.block_size);
		if (rc < 0) {
			break;
		}
		block_remaining = state->logger_state.block_size;
		block_offset = 0;
		state->block_num += 1;

		/* Push all block data into messages */
		while (block_remaining) {
			if (data_buf == NULL) {
				/* Respect any rate-limiting requests from the receiving device */
				epacket_rate_limit_tx(&limit_tx, tx_count);

				/* Allocate new data message */
				data_buf =
					epacket_alloc_tx_for_interface(state->interface, K_FOREVER);
				if (net_buf_tailroom(data_buf) == 0) {
					/* Backend connection has been lost */
					net_buf_unref(data_buf);
					data_buf = NULL;
					state->blocks_remaining = 0;
					break;
				}

				epacket_set_tx_metadata(data_buf, state->auth, 0x00,
							INFUSE_RPC_DATA, state->addr);

				/* Allocate header and calculate packets on first iteration */
				data = net_buf_add(data_buf, sizeof(*data));
				data->request_id = state->request_id;
				data->offset = state->sent_len;
			}

			/* Add as much payload as we can to buffer (aligned to 4 byte chunks) */
			tail = MIN(block_remaining, net_buf_tailroom(data_buf));
			tail = ROUND_DOWN(tail, 4);
			net_buf_add_mem(data_buf, work_mem + block_offset, tail);
			block_remaining -= tail;
			block_offset += tail;

			if (net_buf_tailroom(data_buf) < sizeof(uint32_t)) {
				/* Update sent data CRC (payload only, not the header) */
				state->sent_crc = crc32_ieee_update(state->sent_crc,
								    data_buf->data + sizeof(*data),
								    data_buf->len - sizeof(*data));
				state->sent_len += data_buf->len - sizeof(*data);
				tx_count = data_buf->len;

				/* Send full buffer */
				epacket_queue(state->interface, data_buf);
				data_buf = NULL;
			}
		}
	}
	/* Flush final buffers */
	if ((rc == 0) && (data_buf != NULL)) {
		/* Update sent data CRC (payload only, not the header )*/
		state->sent_crc = crc32_ieee_update(state->sent_crc, data_buf->data + sizeof(*data),
						    data_buf->len - sizeof(*data));
		state->sent_len += data_buf->len - sizeof(*data);

		/* Send full buffer */
		epacket_queue(state->interface, data_buf);
	}
	LOG_DBG("Read complete");

	return 0;
}

static int core_init(struct common_state *state, struct infuse_rpc_req_header *req_header,
		     struct epacket_rx_metadata *req_meta, uint8_t logger)
{
	*state = (struct common_state){
		.interface = req_meta->interface,
		.addr = req_meta->interface_address,
		.auth = req_meta->auth,
		.request_id = req_header->request_id,
	};

	switch (logger) {
#ifdef CONFIG_DATA_LOGGER_FLASH_MAP
	case RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD:
		state->logger = DEVICE_DT_GET_ONE(embeint_data_logger_flash_map);
		break;
#endif /* CONFIG_DATA_LOGGER_FLASH_MAP */
#ifdef CONFIG_DATA_LOGGER_EXFAT
	case RPC_ENUM_DATA_LOGGER_FLASH_REMOVABLE:
		state->logger = DEVICE_DT_GET_ONE(embeint_data_logger_exfat);
		break;
#endif /* CONFIG_DATA_LOGGER_EXFAT */
#ifdef CONFIG_DATA_LOGGER_SHIM
	case RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD:
	case RPC_ENUM_DATA_LOGGER_FLASH_REMOVABLE:
		/* Reroute to the shim logger if enabled */
		state->logger = DEVICE_DT_GET_ONE(embeint_data_logger_shim);
		break;
#endif /* CONFIG_DATA_LOGGER_SHIM */
	default:
		return -ENODEV;
	}

	/* Ensure device initialised properly */
	if (!device_is_ready(state->logger)) {
		return -EBADF;
	}

	/* Populate logger state */
	data_logger_get_state(state->logger, &state->logger_state);
	return 0;
}

struct net_buf *rpc_command_data_logger_read(struct net_buf *request)
{
	struct epacket_rx_metadata *req_meta = net_buf_user_data(request);
	struct rpc_data_logger_read_request *req = (void *)request->data;
	struct rpc_data_logger_read_response rsp = {0};
	struct common_state state;
	int rc = 0;

	/* Commmon initialisation */
	rc = core_init(&state, &req->header, req_meta, req->logger);
	if (rc < 0) {
		goto end;
	}
	state.block_num = req->start_block;

	/* If last block is unbounded, limit it to the data currently present */
	if (req->last_block == UINT32_MAX) {
		req->last_block = state.logger_state.current_block - 1;
	}

	/* Ensure requested data is in range */
	if ((req->start_block < state.logger_state.earliest_block) ||
	    (req->last_block >= state.logger_state.current_block) ||
	    (req->last_block < req->start_block)) {
		rc = -EINVAL;
		goto end;
	}
	state.blocks_remaining = req->last_block - req->start_block + 1;

	/* Free command as we no longer need it and this command can take a while */
	rpc_command_runner_request_unref(request);

	/* Run the data logger read */
	rc = do_read(&state);

	/* Populate output parameters */
	rsp.sent_crc = state.sent_crc;
	rsp.sent_len = state.sent_len;
end:
	return rpc_response_simple_if(state.interface, rc, &rsp, sizeof(rsp));
}

struct net_buf *rpc_command_data_logger_read_available(struct net_buf *request)
{
	struct epacket_rx_metadata *req_meta = net_buf_user_data(request);
	struct rpc_data_logger_read_available_request *req = (void *)request->data;
	struct rpc_data_logger_read_available_response rsp = {0};
	struct common_state state;
	uint32_t blocks_to_end;
	uint32_t block_start;
	int rc = 0;

	/* Commmon initialisation */
	rc = core_init(&state, &req->header, req_meta, req->logger);
	if (rc < 0) {
		goto end;
	}

	/* If blocks earlier than present were requested, jump to earliest data */
	block_start = MAX(req->start_block, state.logger_state.earliest_block);
	state.block_num = block_start;
	blocks_to_end = state.logger_state.current_block - state.block_num;
	state.blocks_remaining = MIN(req->num_blocks, blocks_to_end);

	/* Free command as we no longer need it and this command can take a while */
	rpc_command_runner_request_unref(request);

	/* Run the data logger read */
	rc = do_read(&state);

	/* Populate output parameters */
	data_logger_get_state(state.logger, &state.logger_state);
	rsp.sent_crc = state.sent_crc;
	rsp.sent_len = state.sent_len;
	rsp.current_block = state.logger_state.current_block;
	rsp.start_block_actual = block_start;
	rsp.block_size = state.logger_state.block_size;

end:
	return rpc_response_simple_if(state.interface, rc, &rsp, sizeof(rsp));
}
