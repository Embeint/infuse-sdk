/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/types.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/epacket/interface/epacket_bt_adv.h>
#include <infuse/security.h>

#include "../subsys/epacket/interfaces/epacket_internal.h"

static K_FIFO_DEFINE(handler_fifo);

static void custom_handler(struct net_buf *packet)
{
	k_fifo_put(&handler_fifo, packet);
}

ZTEST(epacket_handlers, test_custom_handler)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = 0x10,
		.auth = EPACKET_AUTH_NETWORK,
		.flags = 0xAFFA,
	};
	struct epacket_rx_metadata *meta;
	uint8_t payload[16] = {0};
	struct net_buf *rx;

	/* Receive without a custom handler */
	epacket_dummy_receive(epacket_dummy, &header, payload, sizeof(payload));
	zassert_is_null(k_fifo_get(&handler_fifo, K_MSEC(100)));

	/* Set the custom handler */
	epacket_set_receive_handler(epacket_dummy, custom_handler);

	/* Receive again with custom handler */
	epacket_dummy_receive(epacket_dummy, &header, payload, sizeof(payload));
	rx = k_fifo_get(&handler_fifo, K_MSEC(100));
	zassert_not_null(rx);
	meta = net_buf_user_data(rx);

	/* Free the buffer */
	net_buf_unref(rx);
}

ZTEST(epacket_handlers, test_key_ids)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct net_buf *tx_bufs[CONFIG_EPACKET_BUFFERS_TX];
	struct epacket_dummy_frame *rx_header;
	struct epacket_key_ids_data *rx_data;
	struct net_buf *rx;

	uint8_t magic_byte = EPACKET_KEY_ID_REQ_MAGIC;

	epacket_dummy_receive(epacket_dummy, NULL, &magic_byte, sizeof(magic_byte));

	/* Standard key ID request */
	rx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_not_null(rx);
	rx_header = (void *)rx->data;
	rx_data = (void *)(rx->data + sizeof(struct epacket_dummy_frame));
	zassert_equal(INFUSE_KEY_IDS, rx_header->type);
	zassert_equal(EPACKET_AUTH_NETWORK, rx_header->auth);
	zassert_equal(sizeof(struct epacket_dummy_frame) + sizeof(struct epacket_key_ids_data),
		      rx->len);
	zassert_equal(infuse_security_device_key_identifier(),
		      sys_get_le24(rx_data->device_key_id));
	net_buf_unref(rx);

	/* Second request immediately should fail */
	epacket_dummy_receive(epacket_dummy, NULL, &magic_byte, sizeof(magic_byte));
	rx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_is_null(rx);

	/* Request after a second should work */
	k_sleep(K_SECONDS(1));
	epacket_dummy_receive(epacket_dummy, NULL, &magic_byte, sizeof(magic_byte));
	rx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_not_null(rx);
	net_buf_unref(rx);

	/* Validate INFUSE_KEY_IDS doesn't block waiting for a buffer in RX processor */
	k_sleep(K_SECONDS(1));
	for (int i = 0; i < ARRAY_SIZE(tx_bufs); i++) {
		tx_bufs[i] = epacket_alloc_tx(K_FOREVER);
	}
	epacket_dummy_receive(epacket_dummy, NULL, &magic_byte, sizeof(magic_byte));
	rx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_is_null(rx);
	for (int i = 0; i < ARRAY_SIZE(tx_bufs); i++) {
		net_buf_unref(tx_bufs[i]);
	}
	/* Ensure processing thread didn't spend this time waiting for a buffer */
	rx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_is_null(rx);

	/* Bad magic byte generates no response */
	magic_byte = 0x02;
	epacket_dummy_receive(epacket_dummy, NULL, &magic_byte, sizeof(magic_byte));
	rx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_is_null(rx);
}

ZTEST(epacket_handlers, test_rate_limiting)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_rate_limit_req rate_limit_req = {
		.magic = EPACKET_RATE_LIMIT_REQ_MAGIC,
	};
	k_ticks_t before, after, diff;
	int wiggle = 2 * MSEC_PER_SEC / (CONFIG_SYS_CLOCK_TICKS_PER_SEC);
	k_ticks_t limit_tx = k_uptime_ticks();

	/* No delays or yields initially */
	before = k_uptime_ticks();
	epacket_rate_limit_tx(&limit_tx, 0);
	after = k_uptime_ticks();
	zassert_equal(before, after, "Unexpected context yield");

	/* Randomly check a range of delays */
	for (int i = 0; i < 20; i++) {
		rate_limit_req.delay_ms = sys_rand8_get();

		/* Rate limit request results in a delay */
		epacket_dummy_receive(epacket_dummy, NULL, &rate_limit_req, sizeof(rate_limit_req));
		k_sleep(K_TICKS(1));

		before = k_uptime_ticks();
		epacket_rate_limit_tx(&limit_tx, 0);
		after = k_uptime_ticks();
		zassert_true(after >= before);
		diff = after - before;
		zassert_between_inclusive(k_ticks_to_ms_near32(diff), rate_limit_req.delay_ms,
					  rate_limit_req.delay_ms + wiggle,
					  "Unexpected rate limit delay");

		/* Additional call doesn't delay or yield */
		before = k_uptime_ticks();
		epacket_rate_limit_tx(&limit_tx, 0);
		after = k_uptime_ticks();
		zassert_equal(before, after, "Unexpected context yield");
	}

	k_sleep(K_MSEC(100));
	zassert_equal(CONFIG_EPACKET_BUFFERS_RX, epacket_num_buffers_free_rx());
	zassert_equal(CONFIG_EPACKET_BUFFERS_TX, epacket_num_buffers_free_tx());
}

ZTEST(epacket_handlers, test_rate_throughput)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_rate_throughput_req rate_throughput_req = {
		.magic = EPACKET_RATE_LIMIT_REQ_MAGIC,
		.target_throughput_kbps = 2,
	};
	k_ticks_t before, after, diff;
	k_ticks_t limit_tx = k_uptime_ticks();

	/* No delays or yields initially */
	before = k_uptime_ticks();
	epacket_rate_limit_tx(&limit_tx, 0);
	after = k_uptime_ticks();
	zassert_equal(before, after, "Unexpected context yield");

	/* Set a target throughput of 2kbps (256 bytes/sec) */
	epacket_dummy_receive(epacket_dummy, NULL, &rate_throughput_req,
			      sizeof(rate_throughput_req));
	k_sleep(K_TICKS(1));

	/* First call, we haven't reported any data sent yet, no delay */
	before = k_uptime_ticks();
	epacket_rate_limit_tx(&limit_tx, 0);
	after = k_uptime_ticks();
	zassert_equal(before, after, "Unexpected context yield");

	/* We've sent 256 bytes (our limit), we should be blocked here for 1 second */
	before = k_uptime_ticks();
	epacket_rate_limit_tx(&limit_tx, 256);
	after = k_uptime_ticks();
	diff = after - before;
	zassert_between_inclusive(diff, k_ms_to_ticks_near32(1000), k_ms_to_ticks_near32(1000) + 2,
				  "Unexpected throughput limit delay");

	/* We've sent 128 bytes (half our limit), we should be blocked here for 500ms overall */
	before = k_uptime_ticks();
	k_sleep(K_MSEC(100));
	epacket_rate_limit_tx(&limit_tx, 128);
	after = k_uptime_ticks();
	diff = after - before;
	zassert_between_inclusive(diff, k_ms_to_ticks_near32(500), k_ms_to_ticks_near32(500) + 2,
				  "Unexpected throughput limit delay");

	/* We've sent 1/4 of the limit in half a second, we shouldn't block */
	k_sleep(K_MSEC(500));
	before = k_uptime_ticks();
	epacket_rate_limit_tx(&limit_tx, 64);
	after = k_uptime_ticks();
	zassert_equal(before, after, "Unexpected context yield");

	/* We've sent 512 bytes (twice our limit), we should be blocked here for 2000ms overall */
	before = k_uptime_ticks();
	k_sleep(K_MSEC(700));
	epacket_rate_limit_tx(&limit_tx, 512);
	after = k_uptime_ticks();
	diff = after - before;
	zassert_between_inclusive(diff, k_ms_to_ticks_near32(2000), k_ms_to_ticks_near32(2000) + 2,
				  "Unexpected throughput limit delay");

	/* Change our throughput to 8kbps (1024 bytes/sec) */
	rate_throughput_req.target_throughput_kbps = 8;
	epacket_dummy_receive(epacket_dummy, NULL, &rate_throughput_req,
			      sizeof(rate_throughput_req));
	k_sleep(K_TICKS(1));

	/* First call, we haven't reported any data sent yet, no delay */
	before = k_uptime_ticks();
	epacket_rate_limit_tx(&limit_tx, 0);
	after = k_uptime_ticks();
	zassert_equal(before, after, "Unexpected context yield");

	/* We've sent 1024 bytes (our limit), we should be blocked here for 1 second */
	before = k_uptime_ticks();
	epacket_rate_limit_tx(&limit_tx, 1024);
	after = k_uptime_ticks();
	diff = after - before;
	zassert_between_inclusive(diff, k_ms_to_ticks_near32(1000), k_ms_to_ticks_near32(1000) + 2,
				  "Unexpected throughput limit delay");

	/* We've sent 512 bytes (half our limit), we should be blocked here for 500ms overall */
	before = k_uptime_ticks();
	k_sleep(K_MSEC(100));
	epacket_rate_limit_tx(&limit_tx, 512);
	after = k_uptime_ticks();
	diff = after - before;
	zassert_between_inclusive(diff, k_ms_to_ticks_near32(500), k_ms_to_ticks_near32(500) + 2,
				  "Unexpected throughput limit delay");

	/* We've sent 1/4 of the limit in half a second, we shouldn't block */
	k_sleep(K_MSEC(500));
	before = k_uptime_ticks();
	epacket_rate_limit_tx(&limit_tx, 512);
	after = k_uptime_ticks();
	zassert_equal(before, after, "Unexpected context yield");

	/* We've sent 2048 bytes (twice our limit), we should be blocked here for 2000ms overall */
	before = k_uptime_ticks();
	k_sleep(K_MSEC(700));
	epacket_rate_limit_tx(&limit_tx, 2048);
	after = k_uptime_ticks();
	diff = after - before;
	zassert_between_inclusive(diff, k_ms_to_ticks_near32(2000), k_ms_to_ticks_near32(2000) + 2,
				  "Unexpected throughput limit delay");

	/* Reset throughput limits, no more delays */
	epacket_rate_limit_reset();

	before = k_uptime_ticks();
	epacket_rate_limit_tx(&limit_tx, 2048);
	epacket_rate_limit_tx(&limit_tx, 2048);
	epacket_rate_limit_tx(&limit_tx, 2048);
	after = k_uptime_ticks();
	zassert_equal(before, after, "Throughput limit reset failure");

	k_sleep(K_MSEC(100));
	zassert_equal(CONFIG_EPACKET_BUFFERS_RX, epacket_num_buffers_free_rx());
	zassert_equal(CONFIG_EPACKET_BUFFERS_TX, epacket_num_buffers_free_tx());
}

ZTEST(epacket_handlers, test_echo_response)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct epacket_dummy_frame *rx_header, header = {0};
	uint8_t payload[64] = {0};
	struct net_buf *rx;

	zassert_not_null(tx_fifo);

	/* Send an echo request */
	header.type = INFUSE_ECHO_REQ;
	header.auth = EPACKET_AUTH_DEVICE;
	epacket_dummy_receive(epacket_dummy, &header, payload, 16);

	rx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_not_null(rx);
	rx_header = (void *)rx->data;
	zassert_equal(INFUSE_ECHO_RSP, rx_header->type);
	zassert_equal(EPACKET_AUTH_DEVICE, rx_header->auth);
	zassert_equal(sizeof(struct epacket_dummy_frame) + 16, rx->len);
	net_buf_unref(rx);

	/* Send a different echo request */
	header.type = INFUSE_ECHO_REQ;
	header.auth = EPACKET_AUTH_NETWORK;
	epacket_dummy_receive(epacket_dummy, &header, payload, 64);

	rx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_not_null(rx);
	rx_header = (void *)rx->data;
	zassert_equal(INFUSE_ECHO_RSP, rx_header->type);
	zassert_equal(EPACKET_AUTH_NETWORK, rx_header->auth);
	zassert_equal(sizeof(struct epacket_dummy_frame) + 64, rx->len);
	net_buf_unref(rx);

	/* Send an auth failure echo request */
	header.type = INFUSE_ECHO_REQ;
	header.auth = EPACKET_AUTH_FAILURE;
	epacket_dummy_receive(epacket_dummy, &header, payload, 16);

	zassert_is_null(k_fifo_get(tx_fifo, K_MSEC(100)));

	k_sleep(K_MSEC(100));
	zassert_equal(CONFIG_EPACKET_BUFFERS_RX, epacket_num_buffers_free_rx());
	zassert_equal(CONFIG_EPACKET_BUFFERS_TX, epacket_num_buffers_free_tx());
}

ZTEST(epacket_handlers, test_echo_no_block)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct epacket_dummy_frame header = {0};
	struct net_buf *tx;
	uint8_t payload[16] = {0};

	zassert_not_null(tx_fifo);

	/* Multiple echo requests don't block */
	zassert_true(CONFIG_EPACKET_BUFFERS_RX > CONFIG_EPACKET_BUFFERS_TX);
	for (int i = 0; i < CONFIG_EPACKET_BUFFERS_RX; i++) {
		header.type = INFUSE_ECHO_REQ;
		header.auth = EPACKET_AUTH_DEVICE;
		epacket_dummy_receive(epacket_dummy, &header, payload, sizeof(payload));
	}
	/* Wait long enough for the receive thread to process all packets */
	k_sleep(K_SECONDS(1));
	/* Validate the number of transmit packets queued */
	for (int i = 0; i < CONFIG_EPACKET_BUFFERS_TX; i++) {
		tx = k_fifo_get(tx_fifo, K_MSEC(100));
		zassert_not_null(tx);
		net_buf_unref(tx);
	}
	zassert_is_null(k_fifo_get(tx_fifo, K_MSEC(100)));

	k_sleep(K_MSEC(100));
	zassert_equal(CONFIG_EPACKET_BUFFERS_RX, epacket_num_buffers_free_rx());
	zassert_equal(CONFIG_EPACKET_BUFFERS_TX, epacket_num_buffers_free_tx());
}

GATEWAY_HANDLER_DEFINE(dummy_backhaul_handler, DEVICE_DT_GET(DT_NODELABEL(epacket_dummy)));

ZTEST(epacket_handlers, test_gateway_fallthrough)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct epacket_dummy_frame header = {0};
	uint8_t payload[16] = {0};

	zassert_not_null(tx_fifo);

	epacket_set_receive_handler(epacket_dummy, dummy_backhaul_handler);

	/* Packet received on dummy interface should just fall through */
	header.type = INFUSE_TDF;
	header.auth = EPACKET_AUTH_DEVICE;
	epacket_dummy_receive(epacket_dummy, &header, payload, sizeof(payload));
	zassert_is_null(k_fifo_get(tx_fifo, K_MSEC(100)));
}

static struct net_buf *create_received_packet(uint8_t type, uint8_t payload_len, bool encrypt)
{
	static const struct device dummy_bt_adv = {
		.name = "epacket_bt_adv",
	};
	const bt_addr_le_t bt_addr_none = {0, {{0, 0, 0, 0, 0, 0}}};
	struct net_buf *buf_tx, *buf_rx;
	struct epacket_tx_metadata *tx_meta;
	struct epacket_rx_metadata *rx_meta;
	uint8_t *p;

	/* Construct the original TX packet */
	buf_tx = epacket_alloc_tx(K_NO_WAIT);
	zassert_not_null(buf_tx);
	net_buf_reserve(buf_tx, sizeof(struct epacket_bt_adv_frame));
	epacket_set_tx_metadata(buf_tx, EPACKET_AUTH_DEVICE, 0, type, EPACKET_ADDR_ALL);
	p = net_buf_add(buf_tx, payload_len);
	sys_rand_get(p, payload_len);

	if (encrypt) {
		zassert_equal(0, epacket_bt_adv_encrypt(buf_tx));
	}
	tx_meta = net_buf_user_data(buf_tx);

	/* Copy across to a received packet */
	buf_rx = epacket_alloc_rx(K_NO_WAIT);
	zassert_not_null(buf_rx);
	net_buf_add_mem(buf_rx, buf_tx->data, buf_tx->len);

	/* Add metadata */
	rx_meta = net_buf_user_data(buf_rx);
	rx_meta->type = type;
	rx_meta->interface = &dummy_bt_adv;
	rx_meta->interface_id = EPACKET_INTERFACE_BT_ADV;
	rx_meta->interface_address.bluetooth = bt_addr_none;
	rx_meta->rssi = -80;
	rx_meta->flags = encrypt ? EPACKET_FLAGS_ENCRYPTION_DEVICE : 0x00;
	rx_meta->auth = encrypt ? EPACKET_AUTH_FAILURE : EPACKET_AUTH_DEVICE;

	/* Free the TX buffer */
	net_buf_unref(buf_tx);
	return buf_rx;
}

static struct net_buf *create_received_tdf_packet(uint8_t payload_len, bool encrypt)
{
	return create_received_packet(INFUSE_TDF, payload_len, encrypt);
}

#ifdef CONFIG_EPACKET_RECEIVE_GROUPING
ZTEST(epacket_handlers, test_gateway_forward)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct epacket_dummy_frame *tx_header = {0};
	struct net_buf *buf_rx, *buf_tx;
	const uint32_t max_hold = CONFIG_EPACKET_RECEIVE_GROUPING_MAX_HOLD_MS;
	uint32_t base_len;

	zassert_not_null(tx_fifo);

	buf_rx = create_received_tdf_packet(60, true);

	/* Packet should not be forwarded out until after MAX_HOLD_MS */
	epacket_gateway_receive_handler(epacket_dummy, buf_rx);
	buf_tx = k_fifo_get(tx_fifo, K_MSEC(max_hold - 50));
	zassert_is_null(buf_tx);
	buf_tx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_not_null(buf_tx);
	tx_header = (void *)buf_tx->data;
	zassert_equal(INFUSE_RECEIVED_EPACKET, tx_header->type);
	zassert_equal(EPACKET_AUTH_DEVICE, tx_header->auth);
	base_len = buf_tx->len;
	net_buf_unref(buf_tx);

	/* A new packet should refesh the timeout */
	buf_rx = create_received_tdf_packet(60, true);
	epacket_gateway_receive_handler(epacket_dummy, buf_rx);
	k_sleep(K_MSEC(max_hold / 2));
	buf_rx = create_received_tdf_packet(60, true);
	epacket_gateway_receive_handler(epacket_dummy, buf_rx);
	buf_tx = k_fifo_get(tx_fifo, K_MSEC(max_hold - 50));
	zassert_is_null(buf_tx);
	buf_tx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_not_null(buf_tx);
	tx_header = (void *)buf_tx->data;
	zassert_equal(INFUSE_RECEIVED_EPACKET, tx_header->type);
	zassert_equal(EPACKET_AUTH_DEVICE, tx_header->auth);
	zassert_true(buf_tx->len > base_len);
	net_buf_unref(buf_tx);

	/* A third packet shouldn't fit, immediate flush and new pending timeout */
	buf_rx = create_received_tdf_packet(60, true);
	epacket_gateway_receive_handler(epacket_dummy, buf_rx);
	buf_rx = create_received_tdf_packet(60, true);
	epacket_gateway_receive_handler(epacket_dummy, buf_rx);
	buf_rx = create_received_tdf_packet(60, true);
	epacket_gateway_receive_handler(epacket_dummy, buf_rx);

	/* The immediate transmission */
	buf_tx = k_fifo_get(tx_fifo, K_MSEC(10));
	zassert_not_null(buf_tx);
	tx_header = (void *)buf_tx->data;
	zassert_equal(INFUSE_RECEIVED_EPACKET, tx_header->type);
	zassert_equal(EPACKET_AUTH_DEVICE, tx_header->auth);
	zassert_true(buf_tx->len > base_len);
	net_buf_unref(buf_tx);

	/* The later flush */
	buf_tx = k_fifo_get(tx_fifo, K_MSEC(max_hold - 50));
	zassert_is_null(buf_tx);
	buf_tx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_not_null(buf_tx);
	tx_header = (void *)buf_tx->data;
	zassert_equal(INFUSE_RECEIVED_EPACKET, tx_header->type);
	zassert_equal(EPACKET_AUTH_DEVICE, tx_header->auth);
	zassert_equal(base_len, buf_tx->len);
	net_buf_unref(buf_tx);

	/* A RPC_RSP packet should immediately flush */
	buf_rx = create_received_tdf_packet(60, true);
	epacket_gateway_receive_handler(epacket_dummy, buf_rx);
	buf_rx = create_received_packet(INFUSE_RPC_RSP, 20, true);
	epacket_gateway_receive_handler(epacket_dummy, buf_rx);

	buf_tx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_not_null(buf_tx);
	tx_header = (void *)buf_tx->data;
	zassert_equal(INFUSE_RECEIVED_EPACKET, tx_header->type);
	zassert_equal(EPACKET_AUTH_DEVICE, tx_header->auth);
	net_buf_unref(buf_tx);

	/* A RPC_RSP packet should immediately send */
	buf_rx = create_received_packet(INFUSE_RPC_RSP, 20, true);
	epacket_gateway_receive_handler(epacket_dummy, buf_rx);

	buf_tx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_not_null(buf_tx);
	tx_header = (void *)buf_tx->data;
	zassert_equal(INFUSE_RECEIVED_EPACKET, tx_header->type);
	zassert_equal(EPACKET_AUTH_DEVICE, tx_header->auth);
	net_buf_unref(buf_tx);

	/* Limit backhaul to be unable to forward */
	epacket_dummy_set_max_packet(20);

	for (int i = 0; i < 10; i++) {
		buf_rx = create_received_tdf_packet(60, true);
		epacket_gateway_receive_handler(epacket_dummy, buf_rx);
	}
	buf_tx = k_fifo_get(tx_fifo, K_MSEC(max_hold + 50));
	zassert_is_null(buf_tx);

	k_sleep(K_MSEC(100));
	zassert_equal(CONFIG_EPACKET_BUFFERS_RX, epacket_num_buffers_free_rx());
	zassert_equal(CONFIG_EPACKET_BUFFERS_TX, epacket_num_buffers_free_tx());
}
#else

ZTEST(epacket_handlers, test_gateway_forward)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct epacket_dummy_frame *tx_header = {0};
	struct net_buf *buf_rx, *buf_tx;

	zassert_not_null(tx_fifo);

	buf_rx = create_received_tdf_packet(60, true);

	/* Expect packet to be forwarded out over epacket_dummy */
	epacket_gateway_receive_handler(epacket_dummy, buf_rx);

	buf_tx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_not_null(buf_tx);
	tx_header = (void *)buf_tx->data;
	zassert_equal(INFUSE_RECEIVED_EPACKET, tx_header->type);
	zassert_equal(EPACKET_AUTH_DEVICE, tx_header->auth);
	net_buf_unref(buf_tx);

	/* Limit backhaul to be unable to forward */
	epacket_dummy_set_max_packet(20);

	for (int i = 0; i < 10; i++) {
		buf_rx = create_received_tdf_packet(60, true);

		epacket_gateway_receive_handler(epacket_dummy, buf_rx);
		zassert_is_null(k_fifo_get(tx_fifo, K_MSEC(100)));
	}

	k_sleep(K_MSEC(100));
	zassert_equal(CONFIG_EPACKET_BUFFERS_RX, epacket_num_buffers_free_rx());
	zassert_equal(CONFIG_EPACKET_BUFFERS_TX, epacket_num_buffers_free_tx());
}

#endif /* CONFIG_EPACKET_RECEIVE_GROUPING */

static bool security_init(const void *global_state)
{
	infuse_security_init();
	return true;
}

static void handler_reset(void *fixture)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));

	/* Reset to default handler before each test */
	epacket_set_receive_handler(epacket_dummy, epacket_default_receive_handler);
	epacket_dummy_set_max_packet(UINT16_MAX);
}

ZTEST_SUITE(epacket_handlers, security_init, NULL, handler_reset, NULL, NULL);
