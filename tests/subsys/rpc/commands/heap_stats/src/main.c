/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>

#include <infuse/states.h>
#include <infuse/types.h>
#include <infuse/rpc/types.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>

static void send_heap_stats_command(uint32_t request_id)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_heap_stats_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_HEAP_STATS,
			},
	};

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static struct net_buf *expect_heap_stats_response(uint32_t request_id)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_heap_stats_response *response;
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

/* Two arbitrary heaps for testing */
K_HEAP_DEFINE(heap1, 512);
K_HEAP_DEFINE(heap2, 1024);

ZTEST(rpc_command_heap_stats, test_basic)
{
	struct rpc_heap_stats_response *response;
	struct net_buf *rsp;
	size_t num_heaps;
	uint32_t free1, free2;
	void *p;

	/* Initial state (no allocations) */
	send_heap_stats_command(3);
	rsp = expect_heap_stats_response(3);
	response = (void *)rsp->data;
	num_heaps = (rsp->len - sizeof(*response)) / sizeof(struct rpc_struct_heap_info);
	zassert_equal(2, num_heaps);

	/* Expected states */
	zassert_equal((uintptr_t)&heap1, response->stats[0].addr);
	zassert_within(512, response->stats[0].free_bytes, 128);
	zassert_equal(0, response->stats[0].allocated_bytes);
	zassert_equal(0, response->stats[0].max_allocated_bytes);
	zassert_equal((uintptr_t)&heap2, response->stats[1].addr);
	zassert_within(1024, response->stats[1].free_bytes, 128);
	zassert_equal(0, response->stats[1].allocated_bytes);
	zassert_equal(0, response->stats[1].max_allocated_bytes);
	free1 = response->stats[0].free_bytes;
	free2 = response->stats[1].free_bytes;
	net_buf_unref(rsp);

	/* Allocate some bytes from each heap */
	p = k_heap_alloc(&heap1, 128, K_FOREVER);
	p = k_heap_alloc(&heap2, 256, K_FOREVER);

	send_heap_stats_command(4);
	rsp = expect_heap_stats_response(4);
	response = (void *)rsp->data;
	num_heaps = (rsp->len - sizeof(*response)) / sizeof(struct rpc_struct_heap_info);
	zassert_equal(2, num_heaps);

	/* Expected states */
	zassert_equal((uintptr_t)&heap1, response->stats[0].addr);
	zassert_within(free1 - 128, response->stats[0].free_bytes, 8);
	zassert_within(128, response->stats[0].allocated_bytes, 8);
	zassert_within(128, response->stats[0].max_allocated_bytes, 8);
	zassert_equal((uintptr_t)&heap2, response->stats[1].addr);
	zassert_within(free2 - 256, response->stats[1].free_bytes, 8);
	zassert_within(256, response->stats[1].allocated_bytes, 8);
	zassert_within(256, response->stats[1].max_allocated_bytes, 8);
	net_buf_unref(rsp);

	/* Free one of the buffers */
	k_heap_free(&heap2, p);

	send_heap_stats_command(4);
	rsp = expect_heap_stats_response(4);
	response = (void *)rsp->data;
	num_heaps = (rsp->len - sizeof(*response)) / sizeof(struct rpc_struct_heap_info);
	zassert_equal(2, num_heaps);

	/* Expected states */
	zassert_equal((uintptr_t)&heap1, response->stats[0].addr);
	zassert_within(free1 - 128, response->stats[0].free_bytes, 8);
	zassert_within(128, response->stats[0].allocated_bytes, 8);
	zassert_within(128, response->stats[0].max_allocated_bytes, 8);
	zassert_equal((uintptr_t)&heap2, response->stats[1].addr);
	zassert_equal(free2, response->stats[1].free_bytes);
	zassert_equal(0, response->stats[1].allocated_bytes);
	zassert_within(256, response->stats[1].max_allocated_bytes, 8);
	net_buf_unref(rsp);
}

ZTEST_SUITE(rpc_command_heap_stats, NULL, NULL, NULL, NULL, NULL);
