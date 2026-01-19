/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>

#include <infuse/rpc/commands.h>
#include <infuse/rpc/command_runner.h>
#include <infuse/rpc/types.h>
#include <infuse/rpc/server.h>
#include <infuse/epacket/packet.h>
#include <infuse/drivers/watchdog.h>

LOG_MODULE_REGISTER(rpc_server, CONFIG_INFUSE_RPC_LOG_LEVEL);

static K_FIFO_DEFINE(command_fifo);
static K_FIFO_DEFINE(data_fifo);
static uint32_t data_packet_acks[RPC_SERVER_MAX_ACK_PERIOD];
static uint8_t data_packet_ack_counter;
static uint8_t command_working_mem[CONFIG_INFUSE_RPC_SERVER_WORKING_MEMORY] __aligned(4);

uint8_t *rpc_server_command_working_mem(size_t *size)
{
	*size = sizeof(command_working_mem);
	return command_working_mem;
}

void rpc_server_queue_command(struct net_buf *buf)
{
	k_fifo_put(&command_fifo, buf);
}

void rpc_server_queue_data(struct net_buf *buf)
{
	k_fifo_put(&data_fifo, buf);
}

struct net_buf *rpc_response_simple_if(const struct device *interface, int16_t rc, void *response,
				       size_t len)
{
	struct net_buf *response_buf = epacket_alloc_tx_for_interface(interface, K_FOREVER);
	struct infuse_rpc_rsp_header *header;

	if (net_buf_tailroom(response_buf) >= len) {
		header = net_buf_add_mem(response_buf, response, len);
		header->return_code = rc;
	}
	return response_buf;
}

struct net_buf *rpc_response_simple_req(struct net_buf *request, int16_t rc, void *response,
					size_t len)
{
	struct epacket_rx_metadata *metadata = net_buf_user_data(request);

	return rpc_response_simple_if(metadata->interface, rc, response, len);
}

IF_DISABLED(CONFIG_ZTEST, (static))
void rpc_server_pull_data_reset(void)
{
	data_packet_ack_counter = 0;
}
static struct net_buf *pull_data_core(uint32_t request_id, uint32_t expected_offset, int *err,
				      k_timeout_t timeout, bool requires_aligned)
{
	struct infuse_rpc_data *data;
	struct net_buf *buf;

	/* Convert any relative timeout to absolute */
	if (Z_TICK_ABS(timeout.ticks) < 0) {
		timeout = K_TIMEOUT_ABS_TICKS(k_uptime_ticks() + timeout.ticks);
	}
	*err = 0;

	/* Loop until we get an INFUSE_RPC_DATA packet for the current command */
	while (true) {
		buf = k_fifo_get(&data_fifo, timeout);
		if (buf == NULL) {
			LOG_WRN("Timeout waiting for offset %08X", expected_offset);
			*err = -ETIMEDOUT;
			return NULL;
		}
		data = (void *)buf->data;
		if (data->request_id != request_id) {
			LOG_WRN("Mismatched request ID (%08X != %08X)", data->request_id,
				request_id);
			net_buf_unref(buf);
			continue;
		}
		if (data->offset != expected_offset) {
			LOG_WRN("Missed data %08X-%08X", expected_offset, data->offset - 1);
		}
		if (requires_aligned && (data->offset % sizeof(uint32_t))) {
			LOG_WRN("Unaligned data offset %08X", data->offset);
			net_buf_unref(buf);
			*err = -EINVAL;
			return NULL;
		}
		/* Server is still alive */
		rpc_server_watchdog_feed();
		return buf;
	}
}

struct net_buf *rpc_server_pull_data(uint32_t request_id, uint32_t expected_offset, int *err,
				     k_timeout_t timeout)
{
	return pull_data_core(request_id, expected_offset, err, timeout, true);
}

struct net_buf *rpc_server_pull_data_unaligned(uint32_t request_id, uint32_t expected_offset,
					       int *err, k_timeout_t timeout)
{
	return pull_data_core(request_id, expected_offset, err, timeout, false);
}

static void send_ack(struct epacket_rx_metadata *rx_meta, uint32_t request_id, uint8_t num_offsets)
{
	struct infuse_rpc_data_ack *data_ack;
	struct net_buf *ack;

	/* Allocate the RPC_DATA_ACK packet */
	ack = epacket_alloc_tx_for_interface(rx_meta->interface, K_FOREVER);
	if (net_buf_tailroom(ack) == 0) {
		net_buf_unref(ack);
		return;
	}
	data_ack = net_buf_add(ack, sizeof(*data_ack));
	epacket_set_tx_metadata_core(ack, EPACKET_AUTH_NETWORK, rx_meta->key_identifier, 0,
				     INFUSE_RPC_DATA_ACK, rx_meta->interface_address);
	/* Populate data */
	data_ack->request_id = request_id;
	net_buf_add_mem(ack, data_packet_acks, num_offsets * sizeof(uint32_t));
	/* Send the RPC_DATA_ACK and reset */
	epacket_queue(rx_meta->interface, ack);
	rpc_server_pull_data_reset();
}

void rpc_server_ack_data_ready(struct epacket_rx_metadata *rx_meta, uint32_t request_id)
{
	send_ack(rx_meta, request_id, 0);
}

void rpc_server_ack_data(struct epacket_rx_metadata *rx_meta, uint32_t request_id, uint32_t offset,
			 uint8_t ack_period)
{
	/* Handle sending ACK responses */
	if (ack_period && (ack_period <= ARRAY_SIZE(data_packet_acks))) {
		/* Store that we received this ack */
		data_packet_acks[data_packet_ack_counter] = offset;
		if (++data_packet_ack_counter >= ack_period) {
			send_ack(rx_meta, request_id, data_packet_ack_counter);
		}
	}
}

INFUSE_WATCHDOG_REGISTER_SYS_INIT(rpc_wdog, CONFIG_INFUSE_RPC_SERVER_WATCHDOG, wdog_channel,
				  loop_period);

void rpc_server_watchdog_feed(void)
{
	infuse_watchdog_feed(wdog_channel);
}

static int rpc_server(void *a, void *b, void *c)
{
	struct k_poll_event events[2] = {
		K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_FIFO_DATA_AVAILABLE,
						K_POLL_MODE_NOTIFY_ONLY, &command_fifo, 0),
		K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_FIFO_DATA_AVAILABLE,
						K_POLL_MODE_NOTIFY_ONLY, &data_fifo, 0),
	};
	struct infuse_rpc_data *data;
	struct net_buf *buf;
	int rc;

	infuse_watchdog_thread_register(wdog_channel, _current);
	while (true) {
		rc = k_poll(events, ARRAY_SIZE(events), loop_period);
		infuse_watchdog_feed(wdog_channel);
		if (rc == -EAGAIN) {
			/* Only woke to feed the watchdog */
			continue;
		}

		if (events[0].state == K_POLL_STATE_FIFO_DATA_AVAILABLE) {
			buf = k_fifo_get(events[0].fifo, K_NO_WAIT);
			rpc_server_pull_data_reset();
			rpc_command_runner(buf);
			events[0].state = K_POLL_STATE_NOT_READY;
		}

		if (events[1].state == K_POLL_STATE_FIFO_DATA_AVAILABLE) {
			buf = k_fifo_get(events[1].fifo, K_NO_WAIT);
			/* Can return NULL if data packet was queued before runner started */
			if (buf) {
				data = (void *)buf->data;
				LOG_WRN("Dropping data for command %08X %08x", data->request_id,
					data->offset);
				net_buf_unref(buf);
			}
			events[1].state = K_POLL_STATE_NOT_READY;
		}

		/* Feed watchdog before sleeping again */
		infuse_watchdog_feed(wdog_channel);
	}
	return 0;
}

K_THREAD_DEFINE(rpc_server_thread, CONFIG_INFUSE_RPC_SERVER_STACK_SIZE, rpc_server, NULL, NULL,
		NULL, CONFIG_INFUSE_RPC_SERVER_THREAD_PRIORITY, K_ESSENTIAL, 0);
