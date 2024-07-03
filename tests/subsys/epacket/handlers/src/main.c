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

uint64_t infuse_device_id(void)
{
	return 0x0123456789ABCDEF;
}

static void custom_handler(struct net_buf *packet)
{
	net_buf_put(&handler_fifo, packet);
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
	zassert_is_null(net_buf_get(&handler_fifo, K_MSEC(100)));

	/* Set the custom handler */
	epacket_set_receive_handler(epacket_dummy, custom_handler);

	/* Receive again with custom handler */
	epacket_dummy_receive(epacket_dummy, &header, payload, sizeof(payload));
	rx = net_buf_get(&handler_fifo, K_MSEC(100));
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
	rx = net_buf_get(tx_fifo, K_MSEC(100));
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
	rx = net_buf_get(tx_fifo, K_MSEC(100));
	zassert_is_null(rx);

	/* Request after a second should work */
	k_sleep(K_SECONDS(1));
	epacket_dummy_receive(epacket_dummy, NULL, &magic_byte, sizeof(magic_byte));
	rx = net_buf_get(tx_fifo, K_MSEC(100));
	zassert_not_null(rx);
	net_buf_unref(rx);

	/* Validate INFUSE_KEY_IDS doesn't block waiting for a buffer in RX processor */
	k_sleep(K_SECONDS(1));
	for (int i = 0; i < ARRAY_SIZE(tx_bufs); i++) {
		tx_bufs[i] = epacket_alloc_tx(K_FOREVER);
	}
	epacket_dummy_receive(epacket_dummy, NULL, &magic_byte, sizeof(magic_byte));
	rx = net_buf_get(tx_fifo, K_MSEC(100));
	zassert_is_null(rx);
	for (int i = 0; i < ARRAY_SIZE(tx_bufs); i++) {
		net_buf_unref(tx_bufs[i]);
	}
	/* Ensure processing thread didn't spend this time waiting for a buffer */
	rx = net_buf_get(tx_fifo, K_MSEC(100));
	zassert_is_null(rx);

	/* Bad magic byte generates no response */
	magic_byte = 0x02;
	epacket_dummy_receive(epacket_dummy, NULL, &magic_byte, sizeof(magic_byte));
	rx = net_buf_get(tx_fifo, K_MSEC(100));
	zassert_is_null(rx);
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

	rx = net_buf_get(tx_fifo, K_MSEC(100));
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

	rx = net_buf_get(tx_fifo, K_MSEC(100));
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

	zassert_is_null(net_buf_get(tx_fifo, K_MSEC(100)));
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
	k_sleep(K_MSEC(1));
	for (int i = 0; i < CONFIG_EPACKET_BUFFERS_TX; i++) {
		tx = net_buf_get(tx_fifo, K_MSEC(100));
		zassert_not_null(tx);
		net_buf_unref(tx);
	}
	zassert_is_null(net_buf_get(tx_fifo, K_MSEC(100)));
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
	zassert_is_null(net_buf_get(tx_fifo, K_MSEC(100)));
}

static struct net_buf *create_received_tdf_packet(uint8_t payload_len, bool encrypt)
{
	const bt_addr_le_t bt_addr_none = {0, {{0, 0, 0, 0, 0, 0}}};
	struct net_buf *buf_tx, *buf_rx;
	struct epacket_tx_metadata *tx_meta;
	struct epacket_rx_metadata *rx_meta;
	uint8_t *p;

	/* Construct the original TX packet */
	buf_tx = epacket_alloc_tx(K_NO_WAIT);
	zassert_not_null(buf_tx);
	net_buf_reserve(buf_tx, sizeof(struct epacket_bt_adv_frame));
	epacket_set_tx_metadata(buf_tx, EPACKET_AUTH_DEVICE, 0, INFUSE_TDF);
	p = net_buf_add(buf_tx, 60);
	sys_rand_get(p, 60);

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
	rx_meta->interface = NULL;
	rx_meta->interface_id = EPACKET_INTERFACE_BT_ADV;
	rx_meta->interface_address.bluetooth = bt_addr_none;
	rx_meta->rssi = -80;
	rx_meta->flags = encrypt ? EPACKET_FLAGS_ENCRYPTION_DEVICE : 0x00;
	rx_meta->auth = encrypt ? EPACKET_AUTH_FAILURE : EPACKET_AUTH_DEVICE;

	/* Free the TX buffer */
	net_buf_unref(buf_tx);
	return buf_rx;
}

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

	buf_tx = net_buf_get(tx_fifo, K_MSEC(100));
	zassert_not_null(buf_tx);
	tx_header = (void *)buf_tx->data;
	zassert_equal(INFUSE_RECEIVED_EPACKET, tx_header->type);
	zassert_equal(EPACKET_AUTH_DEVICE, tx_header->auth);
	net_buf_unref(buf_tx);

	/* Limit backhaul to be unable to forward */
	epacket_dummy_set_max_packet(20);

	for (int i = 0; i < CONFIG_EPACKET_BUFFERS_RX + 1; i++) {
		buf_rx = create_received_tdf_packet(60, true);

		epacket_gateway_receive_handler(epacket_dummy, buf_rx);
		zassert_is_null(net_buf_get(tx_fifo, K_MSEC(100)));
	}
}

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
