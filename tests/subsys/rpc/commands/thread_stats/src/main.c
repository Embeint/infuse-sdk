/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>

#include <infuse/states.h>
#include <infuse/types.h>
#include <infuse/rpc/types.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>

static void send_thread_stats_command(uint32_t request_id)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_thread_stats_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_THREAD_STATS,
			},
	};

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static void expect_thread_stats_response(uint32_t request_id)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_thread_stats_response *response;
	struct epacket_dummy_frame *frame;
	struct infuse_rpc_data *data_header;
	struct rpc_struct_thread_stats *stats;
	struct net_buf *rsp;
	bool pending = true;
	uint16_t threads = 0;
	uint8_t data_packets = 0;

	zassert_not_null(response_queue);

	while (pending) {
		/* Response was sent */
		rsp = k_fifo_get(response_queue, K_SECONDS(1));
		zassert_not_null(rsp);
		frame = net_buf_pull_mem(rsp, sizeof(*frame));

		if (frame->type == INFUSE_RPC_DATA) {
			data_packets += 1;
			data_header = net_buf_pull_mem(rsp, sizeof(*data_header));
			zassert_equal(request_id, data_header->request_id);
			while (rsp->len) {
				stats = net_buf_pull_mem(rsp, sizeof(*stats));
				net_buf_pull(rsp, strlen(stats->name) + 1);
				threads += 1;

				zassert_true(stats->stack_size > 0);
				zassert_true(stats->stack_used > 0);
				zassert_true(stats->utilization >= 0);
				zassert_true(stats->utilization <= 100);
				zassert_true(stats->name > 0);
			}
		} else if (frame->type == INFUSE_RPC_RSP) {
			response = (void *)rsp->data;

			zassert_equal(request_id, response->header.request_id);
			zassert_equal(0, response->header.return_code);
			zassert_equal(threads, response->num_threads);

			printk("%d thread states across %d data packet(s)\n", threads,
			       data_packets);
			pending = false;
		} else {
			zassert_unreachable("Unexpected packet type");
		}

		net_buf_unref(rsp);
	}
}

ZTEST(rpc_command_thread_stats, test_basic)
{
	/* All threads probably fit in single packet */
	send_thread_stats_command(3);
	expect_thread_stats_response(3);

	/* All threads don't fit in single packet */
	epacket_dummy_set_max_packet(64);
	send_thread_stats_command(0x12345678);
	expect_thread_stats_response(0x12345678);
}

ZTEST_SUITE(rpc_command_thread_stats, NULL, NULL, NULL, NULL, NULL);
