/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <zephyr/random/random.h>

#include <infuse/types.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/rpc/types.h>
#include <infuse/rpc/client.h>

struct rpc_echo_req_10 {
	struct rpc_echo_request base;
	uint8_t payload[10];
} __packed;

struct rpc_echo_rsp_10 {
	struct rpc_echo_response base;
	uint8_t payload[10];
} __packed;

static K_SEM_DEFINE(client_cb_sem, 0, 10);
static uint8_t large_buffer[1024];

void epacket_raw_receive_handler(struct net_buf *buf);

static void epacket_loopback(bool require_packet)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	k_timeout_t timeout = require_packet ? K_MSEC(1) : K_NO_WAIT;
	struct epacket_rx_metadata *rx_meta;
	struct net_buf *sent;
	struct net_buf *loop;

	zassert_not_null(sent_queue);

	/* Get any packet that was sent */
	sent = k_fifo_get(sent_queue, timeout);
	if (require_packet) {
		zassert_not_null(sent);
	}
	if (sent == NULL) {
		return;
	}

	loop = epacket_alloc_rx(K_NO_WAIT);
	zassert_not_null(loop);
	net_buf_add_mem(loop, sent->data, sent->len);
	net_buf_unref(sent);

	rx_meta = net_buf_user_data(loop);
	rx_meta->interface = epacket_dummy;
	rx_meta->interface_id = EPACKET_INTERFACE_DUMMY;

	/* Feed back as received packet */
	epacket_raw_receive_handler(loop);
}

static void echo_rsp_empty_cb(const struct net_buf *buf, void *user_data)
{
}

ZTEST(rpc_client, test_invalid)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct rpc_client_ctx ctx;

	rpc_client_init(&ctx, epacket_dummy, EPACKET_ADDR_ALL);

	struct rpc_echo_req_10 req = {.payload = {10, 9, 8, 7, 6, 5, 4, 3, 2, 1}};

	/* Invalid response timeout */
	zassert_equal(-EINVAL,
		      rpc_client_command_queue(&ctx, RPC_ID_ECHO, &req, sizeof(req),
					       echo_rsp_empty_cb, &req, K_SECONDS(1), K_NO_WAIT));
	/* No RPC command parameters */
	zassert_equal(-EINVAL,
		      rpc_client_command_queue(&ctx, RPC_ID_ECHO, NULL, 0, echo_rsp_empty_cb, &req,
					       K_SECONDS(1), K_SECONDS(1)));
	/* No callback */
	zassert_equal(-EINVAL, rpc_client_command_queue(&ctx, RPC_ID_ECHO, &req, sizeof(req), NULL,
							&req, K_SECONDS(1), K_SECONDS(1)));

	/* Cleanup the RPC context */
	rpc_client_cleanup(&ctx);
}

static void echo_rsp_single_cb(const struct net_buf *buf, void *user_data)
{
	zassert_not_null(buf);
	zassert_not_null(user_data);

	struct rpc_echo_req_10 *expected = user_data;
	const struct rpc_echo_rsp_10 *rsp = (const void *)buf->data;

	zassert_equal(0, rsp->base.header.return_code);
	zassert_equal(RPC_ID_ECHO, rsp->base.header.command_id);
	zassert_mem_equal(expected->payload, rsp->payload, sizeof(rsp->payload));

	k_sem_give(&client_cb_sem);
}

ZTEST(rpc_client, test_single)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct rpc_client_ctx ctx;
	int rc;

	rpc_client_init(&ctx, epacket_dummy, EPACKET_ADDR_ALL);

	struct rpc_echo_req_10 req = {.payload = {10, 9, 8, 7, 6, 5, 4, 3, 2, 1}};

	rc = rpc_client_command_queue(&ctx, RPC_ID_ECHO, &req, sizeof(req), echo_rsp_single_cb,
				      &req, K_NO_WAIT, K_SECONDS(1));
	zassert_equal(0, rc);

	/* Wait a little bit */
	k_sleep(K_MSEC(100));
	/* Forward RPC_CMD back into our own RPC server implementation */
	epacket_loopback(true);
	/* Wait a bit longer */
	k_sleep(K_MSEC(100));
	/* Send the RPC_RSP back into the receive handler */
	epacket_loopback(true);

	/* Expect the client callback to run */
	zassert_equal(0, k_sem_take(&client_cb_sem, K_MSEC(100)));

	/* Cleanup the RPC context */
	rpc_client_cleanup(&ctx);
}

ZTEST(rpc_client, test_unknown_rsp)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct rpc_client_ctx ctx;
	int rc;

	rpc_client_init(&ctx, epacket_dummy, EPACKET_ADDR_ALL);

	struct rpc_echo_req_10 req = {.payload = {10, 9, 8, 7, 6, 5, 4, 3, 2, 1}};

	rc = rpc_client_command_queue(&ctx, RPC_ID_ECHO, &req, sizeof(req), echo_rsp_single_cb,
				      &req, K_NO_WAIT, K_SECONDS(1));
	zassert_equal(0, rc);

	/* Wait a little bit */
	k_sleep(K_MSEC(100));
	/* Feed an unknown RPC_RSP ID back into the handler */
	struct epacket_dummy_frame hdr = {
		.type = INFUSE_RPC_RSP,
		.auth = EPACKET_AUTH_DEVICE,
	};
	struct infuse_rpc_rsp_header rpc_rsp_hdr = {
		.command_id = RPC_ID_ECHO,
		.request_id = ctx.request_id + 100,
		.return_code = 0,
	};

	epacket_dummy_receive(epacket_dummy, &hdr, &rpc_rsp_hdr, sizeof(rpc_rsp_hdr));

	/* Feed a mismatching command ID back into the handler */
	rpc_rsp_hdr.request_id = ctx.request_id;
	rpc_rsp_hdr.command_id += 1;

	epacket_dummy_receive(epacket_dummy, &hdr, &rpc_rsp_hdr, sizeof(rpc_rsp_hdr));

	/* Neither should have triggered the response callback */
	zassert_equal(-EAGAIN, k_sem_take(&client_cb_sem, K_MSEC(100)));

	/* Forward RPC_CMD back into our own RPC server implementation */
	epacket_loopback(true);
	/* Wait a bit longer */
	k_sleep(K_MSEC(100));
	/* Send the RPC_RSP back into the receive handler */
	epacket_loopback(true);

	/* Expect the client callback to run */
	zassert_equal(0, k_sem_take(&client_cb_sem, K_MSEC(100)));

	/* Cleanup the RPC context */
	rpc_client_cleanup(&ctx);
}

static void echo_rsp_timeout_cb(const struct net_buf *buf, void *user_data)
{
	zassert_is_null(buf);

	k_sem_give(&client_cb_sem);
}

ZTEST(rpc_client, test_rsp_timeout)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_client_ctx ctx;
	struct net_buf *sent;
	int rc;

	zassert_not_null(sent_queue);

	rpc_client_init(&ctx, epacket_dummy, EPACKET_ADDR_ALL);

	struct rpc_echo_req_10 req = {.payload = {10, 9, 8, 7, 6, 5, 4, 3, 2, 1}};

	rc = rpc_client_command_queue(&ctx, RPC_ID_ECHO, &req, sizeof(req), echo_rsp_timeout_cb,
				      &req, K_NO_WAIT, K_MSEC(1000));
	zassert_equal(0, rc);

	/* Let the command time out */
	k_sleep(K_MSEC(1100));

	/* Discard the sent packet */
	sent = k_fifo_get(sent_queue, K_MSEC(1));
	zassert_not_null(sent);
	net_buf_unref(sent);

	/* Callback should have been run */
	zassert_equal(0, k_sem_take(&client_cb_sem, K_NO_WAIT));

	/* Cleanup the RPC context */
	rpc_client_cleanup(&ctx);
}

ZTEST(rpc_client, test_rsp_early_cleanup)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_client_ctx ctx;
	int rc;

	zassert_not_null(sent_queue);

	rpc_client_init(&ctx, epacket_dummy, EPACKET_ADDR_ALL);

	struct rpc_echo_req_10 req = {.payload = {10, 9, 8, 7, 6, 5, 4, 3, 2, 1}};

	rc = rpc_client_command_queue(&ctx, RPC_ID_ECHO, &req, sizeof(req), echo_rsp_timeout_cb,
				      &req, K_NO_WAIT, K_MSEC(1000));
	zassert_equal(0, rc);

	k_sleep(K_MSEC(100));

	/* Cleanup the RPC context */
	rpc_client_cleanup(&ctx);

	/* Callback should have been run */
	zassert_equal(0, k_sem_take(&client_cb_sem, K_MSEC(1)));

	/* Wait a little while */
	k_sleep(K_MSEC(100));

	/* Handle the packet */
	epacket_loopback(true);
	epacket_loopback(true);

	/* Semaphore shouldn't be given, nothing should break */
	zassert_equal(-EAGAIN, k_sem_take(&client_cb_sem, K_MSEC(100)));
}

static void echo_rsp_multi_cb(const struct net_buf *buf, void *user_data)
{
	zassert_not_null(buf);
	zassert_is_null(user_data);

	const struct rpc_echo_rsp_10 *rsp = (const void *)buf->data;

	zassert_equal(0, rsp->base.header.return_code);
	zassert_equal(RPC_ID_ECHO, rsp->base.header.command_id);

	k_sem_give(&client_cb_sem);
}

ZTEST(rpc_client, test_multi)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct rpc_client_ctx ctx;
	int rc;

	struct rpc_echo_req_10 req = {.payload = {50, 9, 8, 7, 6, 5, 4, 3, 2, 1}};

	rpc_client_init(&ctx, epacket_dummy, EPACKET_ADDR_ALL);

	/* Push multiple commands */
	for (int i = 0; i < CONFIG_INFUSE_RPC_CLIENT_MAX_IN_FLIGHT; i++) {
		rc = rpc_client_command_queue(&ctx, RPC_ID_ECHO, &req, sizeof(req),
					      echo_rsp_multi_cb, NULL, K_NO_WAIT, K_SECONDS(1));
		zassert_equal(0, rc);
	}

	/* Additional push should timeout */
	rc = rpc_client_command_queue(&ctx, RPC_ID_ECHO, &req, sizeof(req), echo_rsp_multi_cb, NULL,
				      K_NO_WAIT, K_SECONDS(1));
	zassert_equal(-EAGAIN, rc);

	/* Process the commands after a while */
	k_sleep(K_MSEC(100));
	for (int i = 0; i < CONFIG_INFUSE_RPC_CLIENT_MAX_IN_FLIGHT; i++) {
		epacket_loopback(true);
		epacket_loopback(true);
	}
	k_sleep(K_MSEC(100));

	for (int i = 0; i < CONFIG_INFUSE_RPC_CLIENT_MAX_IN_FLIGHT; i++) {
		zassert_equal(0, k_sem_take(&client_cb_sem, K_MSEC(100)));
	}

	/* Validate request ID rollover doesn't cause problems */
	ctx.request_id = UINT32_MAX - 1;
	for (int i = 0; i < CONFIG_INFUSE_RPC_CLIENT_MAX_IN_FLIGHT; i++) {
		rc = rpc_client_command_queue(&ctx, RPC_ID_ECHO, &req, sizeof(req),
					      echo_rsp_multi_cb, NULL, K_NO_WAIT, K_SECONDS(1));
		zassert_equal(0, rc);
	}
	k_sleep(K_MSEC(100));
	for (int i = 0; i < CONFIG_INFUSE_RPC_CLIENT_MAX_IN_FLIGHT; i++) {
		epacket_loopback(true);
		epacket_loopback(true);
	}
	k_sleep(K_MSEC(100));

	for (int i = 0; i < CONFIG_INFUSE_RPC_CLIENT_MAX_IN_FLIGHT; i++) {
		zassert_equal(0, k_sem_take(&client_cb_sem, K_MSEC(100)));
	}

	/* Cleanup the RPC context */
	rpc_client_cleanup(&ctx);
}

static void async_processor(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);

	epacket_loopback(false);

	k_work_reschedule(dwork, K_MSEC(100));
}

static uint32_t expected_len;
static uint32_t expected_crc;

static void command_data_done(const struct net_buf *buf, void *user_data)
{
	zassert_not_null(buf);
	zassert_is_null(user_data);

	const struct rpc_data_receiver_response *rsp = (const void *)buf->data;

	zassert_equal(0, rsp->header.return_code);
	zassert_equal(RPC_ID_DATA_RECEIVER, rsp->header.command_id);
	zassert_equal(expected_len, rsp->recv_len);
	zassert_equal(expected_crc, rsp->recv_crc);

	k_sem_give(&client_cb_sem);
}

static void test_command_data_param(uint32_t size, uint8_t ack_period, bool single)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_work_delayable dwork;
	struct rpc_client_ctx ctx;
	struct rpc_data_receiver_request req = {
		.data_header =
			{
				.size = size,
				.rx_ack_period = ack_period,
			},
	};
	uint8_t buffer[128];
	uint32_t request_id;
	uint32_t remaining, offset;
	uint8_t pkt_cnt;
	int rc;

	expected_len = size;
	expected_crc = 0;

	/* Limit backend to a weird payload size to exercise word-alignment logic */
	epacket_dummy_set_max_packet(117);

	/* Need to do ePacket loopback in an alternate context for blocking API */
	k_work_init_delayable(&dwork, async_processor);
	k_work_reschedule(&dwork, K_MSEC(100));

	rpc_client_init(&ctx, epacket_dummy, EPACKET_ADDR_ALL);

	rc = rpc_client_command_queue(&ctx, RPC_ID_DATA_RECEIVER, &req, sizeof(req),
				      command_data_done, NULL, K_NO_WAIT, K_SECONDS(1));
	zassert_equal(0, rc);
	request_id = rpc_client_last_request_id(&ctx);

	/* Using a bad response ID fails */
	zassert_equal(-EINVAL, rpc_client_ack_wait(&ctx, request_id + 1, K_FOREVER));
	zassert_equal(-EINVAL, rpc_client_data_queue(&ctx, request_id + 1, 0, buffer, 10));
	zassert_equal(-EINVAL,
		      rpc_client_update_response_timeout(&ctx, request_id + 1, K_SECONDS(5)));

	/* Wait for initial ACK */
	zassert_equal(0, rpc_client_ack_wait(&ctx, request_id, K_SECONDS(1)));

	/* Drop timeout value */
	zassert_equal(0, rpc_client_update_response_timeout(&ctx, request_id, K_MSEC(950)));

	/* Expect non word-aligned offsets to fail */
	for (int i = 1; i < sizeof(uint32_t); i++) {
		zassert_equal(-EINVAL, rpc_client_data_queue(&ctx, request_id, i, buffer, 16));
	}

	/* Push requested data size */
	remaining = req.data_header.size;
	offset = 0;
	pkt_cnt = 0;
	while (remaining > 0) {
		size_t to_send;
		const uint8_t *p;

		if (single) {
			to_send = size;
			p = large_buffer;
		} else {
			to_send = MIN(remaining, sizeof(buffer));
			p = buffer;
		}

		expected_crc = crc32_ieee_update(expected_crc, p, to_send);
		rc = rpc_client_data_queue(&ctx, request_id, offset, p, to_send);
		zassert_equal(0, rc);

		if (offset == 0) {
			/* Feed in a bad ACK */
			struct epacket_dummy_frame hdr = {
				.type = INFUSE_RPC_DATA_ACK,
				.auth = EPACKET_AUTH_NETWORK,
			};
			struct infuse_rpc_data_ack ack_hdr = {
				.request_id = request_id + 1,
			};

			epacket_dummy_receive(epacket_dummy, &hdr, &ack_hdr, sizeof(ack_hdr));
		}

		remaining -= to_send;
		offset += to_send;

		/* Can't be greedy with rpc_client_data_queue in the test environment
		 * as the loopback logic also needs to claim buffers from the same pool.
		 */
		k_sleep(K_MSEC(250));

		/* Wait for ACKs */
		if (remaining && (++pkt_cnt == req.data_header.rx_ack_period)) {
			zassert_equal(0, rpc_client_ack_wait(&ctx, request_id, K_SECONDS(1)));
			pkt_cnt = 0;
		}
	}

	/* Final callback should have run */
	zassert_equal(0, k_sem_take(&client_cb_sem, K_MSEC(1000)));

	/* Queuing after command completion should return an error */
	zassert_equal(-EINVAL, rpc_client_data_queue(&ctx, request_id, offset, buffer, 10));
	zassert_equal(-EINVAL, rpc_client_ack_wait(&ctx, request_id, K_FOREVER));
	zassert_equal(-EINVAL, rpc_client_update_response_timeout(&ctx, request_id, K_SECONDS(2)));

	/* Cancel loopback worker */
	k_work_cancel_delayable(&dwork);

	/* Cleanup the RPC context */
	rpc_client_cleanup(&ctx);
}

ZTEST(rpc_client, test_command_data)
{
	sys_rand_get(large_buffer, sizeof(large_buffer));

	test_command_data_param(1000, 1, false);
	test_command_data_param(5000, 2, false);
	test_command_data_param(4000, 3, false);
	test_command_data_param(512, 1, true);
}

static int data_loader(void *user_data, uint32_t offset, void *data, size_t data_len)
{
	ARG_UNUSED(offset);

	/* Condition for memcpy to not read off end of buffer */
	zassert_true(data_len <= sizeof(large_buffer));

	/* Delay the sending for a while to enable the loopback logic to run */
	k_sleep(K_MSEC(250));

	/* Load the next data chunk */
	memcpy(data, large_buffer, data_len);

	/* Update expected CRC */
	expected_crc = crc32_ieee_update(expected_crc, data, data_len);
	return 0;
}

static void test_command_data_param_auto_loader(uint32_t size, uint8_t ack_period,
						uint8_t pipelining)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_work_delayable dwork;
	struct rpc_client_ctx ctx;
	struct rpc_data_receiver_request req = {
		.data_header =
			{
				.size = size,
				.rx_ack_period = ack_period,
			},
	};
	uint8_t buffer[256];
	uint32_t request_id;
	int rc;

	expected_len = size;
	expected_crc = 0;

	/* Limit backend to a weird payload size to exercise word-alignment logic */
	epacket_dummy_set_max_packet(117);

	/* Need to do ePacket loopback in an alternate context for blocking API */
	k_work_init_delayable(&dwork, async_processor);
	k_work_reschedule(&dwork, K_MSEC(100));

	rpc_client_init(&ctx, epacket_dummy, EPACKET_ADDR_ALL);

	rc = rpc_client_command_queue(&ctx, RPC_ID_DATA_RECEIVER, &req, sizeof(req),
				      command_data_done, NULL, K_NO_WAIT, K_SECONDS(1));
	zassert_equal(0, rc);
	request_id = rpc_client_last_request_id(&ctx);

	/* Using a bad response ID fails */
	zassert_equal(-EINVAL, rpc_client_ack_wait(&ctx, request_id + 1, K_FOREVER));
	zassert_equal(-EINVAL, rpc_client_data_queue(&ctx, request_id + 1, 0, buffer, 10));
	zassert_equal(-EINVAL,
		      rpc_client_update_response_timeout(&ctx, request_id + 1, K_SECONDS(5)));

	/* Wait for initial ACK */
	zassert_equal(0, rpc_client_ack_wait(&ctx, request_id, K_SECONDS(1)));

	/* Drop timeout value */
	zassert_equal(0, rpc_client_update_response_timeout(&ctx, request_id, K_MSEC(950)));

	/* Expect non word-aligned offsets to fail */
	for (int i = 1; i < sizeof(uint32_t); i++) {
		zassert_equal(-EINVAL, rpc_client_data_queue(&ctx, request_id, i, buffer, 16));
	}

	struct rpc_client_auto_load_params loader_params = {
		.loader = data_loader,
		.total_len = size,
		.ack_wait = K_MSEC(1000),
		.ack_period = ack_period,
		.pipelining = pipelining,
		.user_data = NULL,
	};

	printk("Running auto load: Size %5d bytes, ACK %d, Pipelining %d\n", size, ack_period,
	       pipelining);

	/* Push requested data size */
	rc = rpc_client_data_queue_auto_load(&ctx, request_id, 0, buffer, sizeof(buffer),
					     &loader_params);
	zassert_equal(0, rc);

	/* Final callback should have run */
	zassert_equal(0, k_sem_take(&client_cb_sem, K_MSEC(1000)));

	/* Queuing after command completion should return an error */
	zassert_equal(-EINVAL, rpc_client_data_queue_auto_load(&ctx, request_id, 10, buffer,
							       sizeof(buffer), &loader_params));
	zassert_equal(-EINVAL, rpc_client_ack_wait(&ctx, request_id, K_FOREVER));
	zassert_equal(-EINVAL, rpc_client_update_response_timeout(&ctx, request_id, K_SECONDS(2)));

	/* Cancel loopback worker */
	k_work_cancel_delayable(&dwork);

	/* Cleanup the RPC context */
	rpc_client_cleanup(&ctx);
}

ZTEST(rpc_client, test_command_data_auto_loader)
{
	sys_rand_get(large_buffer, sizeof(large_buffer));

	test_command_data_param_auto_loader(1000, 1, 0);
	test_command_data_param_auto_loader(3200, 1, 3);
	test_command_data_param_auto_loader(5000, 2, 3);
	test_command_data_param_auto_loader(4000, 3, 2);
	test_command_data_param_auto_loader(512, 1, 1);
	test_command_data_param_auto_loader(107, 1, 2);
}

ZTEST(rpc_client, test_sync)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_work_delayable dwork;
	struct rpc_client_ctx ctx;
	struct net_buf *rsp;
	int rc;

	struct rpc_echo_req_10 req = {.payload = {100, 5, 8, 7, 6, 5, 4, 3, 2, 1}};
	struct rpc_echo_rsp_10 *echo_rsp;

	/* Need to do ePacket loopback in an alternate context for blocking API */
	k_work_init_delayable(&dwork, async_processor);
	k_work_reschedule(&dwork, K_MSEC(100));

	rpc_client_init(&ctx, epacket_dummy, EPACKET_ADDR_ALL);

	/* Run a bunch of synchronous commands */
	for (int i = 0; i < 10; i++) {
		req.payload[4] += 1;

		/* Run the synchronous command */
		rc = rpc_client_command_sync(&ctx, RPC_ID_ECHO, &req, sizeof(req), K_NO_WAIT,
					     K_SECONDS(1), &rsp);
		zassert_equal(0, rc);
		zassert_not_null(rsp);

		echo_rsp = (void *)rsp->data;
		zassert_equal(RPC_ID_ECHO, echo_rsp->base.header.command_id);
		zassert_equal(0, echo_rsp->base.header.return_code);
		zassert_mem_equal(req.payload, echo_rsp->payload, sizeof(echo_rsp->payload));

		net_buf_unref(rsp);
	}

	/* Consume all the command contexts */
	for (int i = 0; i < CONFIG_INFUSE_RPC_CLIENT_MAX_IN_FLIGHT; i++) {
		rc = rpc_client_command_queue(&ctx, RPC_ID_ECHO, &req, sizeof(req),
					      echo_rsp_single_cb, &req, K_NO_WAIT, K_SECONDS(1));
		zassert_equal(0, rc);
	}
	/* Attempt to run the synchronous command */
	rc = rpc_client_command_sync(&ctx, RPC_ID_ECHO, &req, sizeof(req), K_NO_WAIT, K_SECONDS(1),
				     &rsp);
	zassert_equal(-EAGAIN, rc);

	/* Wait for the async commands to finish... */
	for (int i = 0; i < CONFIG_INFUSE_RPC_CLIENT_MAX_IN_FLIGHT; i++) {
		zassert_equal(0, k_sem_take(&client_cb_sem, K_SECONDS(1)));
	}

	/* Synchronous command should work again */
	rc = rpc_client_command_sync(&ctx, RPC_ID_ECHO, &req, sizeof(req), K_NO_WAIT, K_SECONDS(1),
				     &rsp);
	zassert_equal(0, rc);
	zassert_not_null(rsp);
	net_buf_unref(rsp);

	/* Cancel loopback worker */
	k_work_cancel_delayable(&dwork);

	/* Run a synchronous command that will timeout */
	rc = rpc_client_command_sync(&ctx, RPC_ID_ECHO, &req, sizeof(req), K_NO_WAIT, K_SECONDS(1),
				     &rsp);
	zassert_equal(-ETIMEDOUT, rc);
	zassert_is_null(rsp);

	/* Cleanup the RPC context */
	rpc_client_cleanup(&ctx);
}

void test_cleanup(void *fixture)
{
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	struct net_buf *sent;

	/* Purge any pending commands */
	while (true) {
		sent = k_fifo_get(sent_queue, K_NO_WAIT);
		if (sent) {
			net_buf_unref(sent);
		} else {
			break;
		}
	}

	epacket_dummy_set_max_packet(EPACKET_INTERFACE_MAX_PACKET(DT_NODELABEL(epacket_dummy)));
}

ZTEST_SUITE(rpc_client, NULL, NULL, NULL, test_cleanup, NULL);
