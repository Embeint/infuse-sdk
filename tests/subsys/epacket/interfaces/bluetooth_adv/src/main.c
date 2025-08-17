/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>

#include <infuse/identifiers.h>
#include <infuse/security.h>
#include <infuse/epacket/keys.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_bt_adv.h>

#include "../subsys/epacket/interfaces/epacket_internal.h"

#ifndef CONFIG_BT
/* From addr.c */
const bt_addr_le_t bt_addr_le_any = {0, {{0, 0, 0, 0, 0, 0}}};
#endif

ZTEST(epacket_bt_adv, test_address)
{
	union epacket_interface_address all = EPACKET_ADDR_ALL;

	zassert_true(bt_addr_le_eq(&all.bluetooth, BT_ADDR_LE_ANY));
}

ZTEST(epacket_bt_adv, test_metadata)
{
	struct epacket_rx_metadata *meta;
	enum epacket_auth iter_auth;
	struct net_buf *tx, *rx;
	uint16_t seqs[8];
	uint8_t *p;
	int rc;

	rx = epacket_alloc_rx(K_NO_WAIT);
	zassert_not_null(rx);

	for (int i = 0; i < ARRAY_SIZE(seqs); i++) {
		iter_auth = i % 2 ? EPACKET_AUTH_DEVICE : EPACKET_AUTH_NETWORK;

		/* Construct buffer */
		tx = epacket_alloc_tx(K_NO_WAIT);
		zassert_not_null(tx);
		net_buf_reserve(tx, sizeof(struct epacket_bt_adv_frame));
		epacket_set_tx_metadata(tx, iter_auth, i, 0x10 + i, EPACKET_ADDR_ALL);
		p = net_buf_add(tx, 60);
		sys_rand_get(p, 60);

		/* Encrypt payload */
		rc = epacket_bt_adv_encrypt(tx);
		zassert_equal(0, rc);

		/* Copy message contents across to RX buffer */
		net_buf_reset(rx);
		net_buf_add_mem(rx, tx->data, tx->len);
		net_buf_unref(tx);

		/* Decrypt and free */
		rc = epacket_bt_adv_decrypt(rx);
		zassert_equal(0, rc);
		meta = net_buf_user_data(rx);
		zassert_equal(iter_auth, meta->auth);
		zassert_equal(0x10 + i, meta->type);
		zassert_equal(infuse_device_id(), meta->packet_device_id);
		zassert_not_equal(0, meta->packet_gps_time);
		if (iter_auth == EPACKET_AUTH_DEVICE) {
			zassert_equal(EPACKET_FLAGS_ENCRYPTION_DEVICE | i, meta->flags);
			zassert_equal(infuse_security_device_key_identifier(),
				      meta->key_identifier);
		} else {
			zassert_equal(EPACKET_FLAGS_ENCRYPTION_NETWORK | i, meta->flags);
			zassert_equal(infuse_security_network_key_identifier(),
				      meta->key_identifier);
		}
		seqs[i] = meta->sequence;

		if (i > 0) {
			/* Sequence number should increase on each packet */
			zassert_equal(seqs[i - 1] + 1, seqs[i]);
		}
	}
	net_buf_unref(rx);
}

ZTEST(epacket_bt_adv, test_decrypt_error)
{
	struct net_buf *rx;
	uint8_t payload[64];

	for (int i = 1; i <= sizeof(struct epacket_bt_adv_frame) + 16; i++) {
		/* Create too small buffer */
		rx = epacket_alloc_rx(K_NO_WAIT);
		zassert_not_null(rx);
		net_buf_add_mem(rx, payload, i);

		/* Ensure decode errors */
		zassert_equal(-1, epacket_bt_adv_decrypt(rx));
		net_buf_unref(rx);
	}
}

static void test_encrypt_decrypt_auth(enum epacket_auth auth)
{
	struct net_buf *orig_buf, *encr_buf, *rx, *rx_copy_buf, *in;
	struct epacket_rx_metadata *meta;
	uint8_t *p;
	int rc;

	/* Create original buffer */
	orig_buf = epacket_alloc_tx(K_NO_WAIT);
	zassert_not_null(orig_buf);
	net_buf_reserve(orig_buf, sizeof(struct epacket_bt_adv_frame));
	epacket_set_tx_metadata(orig_buf, auth, 0, 0x10, EPACKET_ADDR_ALL);
	p = net_buf_add(orig_buf, 60);
	sys_rand_get(p, 60);

	/* Encrypt original buffer */
	encr_buf = net_buf_clone(orig_buf, K_NO_WAIT);
	memcpy(encr_buf->user_data, orig_buf->user_data, encr_buf->user_data_size);
	zassert_not_null(encr_buf);
	rc = epacket_bt_adv_encrypt(encr_buf);
	zassert_equal(0, rc);
	zassert_equal(orig_buf->len + sizeof(struct epacket_bt_adv_frame) + 16, encr_buf->len);

	/* Copy message contents across to RX buffer */
	rx = epacket_alloc_rx(K_NO_WAIT);
	zassert_not_null(rx);
	net_buf_add_mem(rx, encr_buf->data, encr_buf->len);
	net_buf_unref(encr_buf);

	/* Decrypt unmodified packet */
	rx_copy_buf = net_buf_clone(rx, K_NO_WAIT);
	zassert_not_null(rx_copy_buf);
	rc = epacket_bt_adv_decrypt(rx_copy_buf);
	zassert_equal(0, rc);
	zassert_equal(orig_buf->len, rx_copy_buf->len);
	zassert_mem_equal(orig_buf->data, rx_copy_buf->data, rx_copy_buf->len);
	net_buf_unref(rx_copy_buf);

	/* Test failure on modified encryption buffer */
	for (int i = 0; i < rx->len; i++) {
		rx_copy_buf = net_buf_clone(rx, K_NO_WAIT);
		zassert_not_null(rx_copy_buf);
		rx_copy_buf->data[i]++;
		in = net_buf_clone(rx_copy_buf, K_NO_WAIT);
		zassert_not_null(in);
		rc = epacket_bt_adv_decrypt(rx_copy_buf);
		meta = net_buf_user_data(rx_copy_buf);
		zassert_equal(-1, rc);
		zassert_equal(EPACKET_AUTH_FAILURE, meta->auth);
		zassert_equal(in->len, rx_copy_buf->len);
		zassert_mem_equal(in->data, rx_copy_buf->data, in->len);
		net_buf_unref(rx_copy_buf);
		net_buf_unref(in);
	}
	net_buf_unref(orig_buf);
	net_buf_unref(rx);
}

ZTEST(epacket_bt_adv, test_encrypt_decrypt)
{
	test_encrypt_decrypt_auth(EPACKET_AUTH_DEVICE);
	test_encrypt_decrypt_auth(EPACKET_AUTH_NETWORK);
}

static struct {
	const uint8_t *payload_ptr;
	size_t payload_len;
} parse_state;

bool parse_func(struct bt_data *data, void *user_data)
{
	if (data->type == BT_DATA_MANUFACTURER_DATA) {
		/* Minus the manufacturer ID */
		parse_state.payload_ptr = data->data + sizeof(uint16_t);
		parse_state.payload_len = data->data_len - sizeof(uint16_t);
	}
	return true;
}

ZTEST(epacket_bt_adv, test_ad_serialization)
{
	NET_BUF_SIMPLE_DEFINE(flat_buffer, 256);
	struct net_buf *orig_buf;
	struct bt_data *data;
	size_t ad_num;
	uint8_t *p;

	epacket_bt_adv_ad_init();

	/* Create random original buffer */
	orig_buf = epacket_alloc_tx(K_NO_WAIT);
	zassert_not_null(orig_buf);
	p = net_buf_add(orig_buf, 60);
	sys_rand_get(p, 60);

	/* Serialise it to an AD structure */
	data = epacket_bt_adv_pkt_to_ad(orig_buf, &ad_num);
	zassert_not_null(data);
	zassert_equal(3, ad_num);

	/* Serialize packet to a flat array */
	net_buf_simple_reset(&flat_buffer);
	flat_buffer.len += bt_data_serialize(&data[0], net_buf_simple_tail(&flat_buffer));
	flat_buffer.len += bt_data_serialize(&data[1], net_buf_simple_tail(&flat_buffer));
	flat_buffer.len += bt_data_serialize(&data[2], net_buf_simple_tail(&flat_buffer));

	/* Parse flat array using Bluetooth parser */
	bt_data_parse(&flat_buffer, parse_func, 0);

	/* Ensure parsed output matches input */
	zassert_not_null(parse_state.payload_ptr);
	zassert_equal(orig_buf->len, parse_state.payload_len);
	zassert_mem_equal(orig_buf->data, parse_state.payload_ptr, orig_buf->len);

	/* Re-serialize packet */
	net_buf_simple_reset(&flat_buffer);
	flat_buffer.len += bt_data_serialize(&data[0], net_buf_simple_tail(&flat_buffer));
	flat_buffer.len += bt_data_serialize(&data[1], net_buf_simple_tail(&flat_buffer));
	flat_buffer.len += bt_data_serialize(&data[2], net_buf_simple_tail(&flat_buffer));

	/* Our detection function should pass, and buffers match */
	zassert_true(epacket_bt_adv_is_epacket(BT_GAP_ADV_TYPE_EXT_ADV, &flat_buffer));
	zassert_equal(orig_buf->len, flat_buffer.len);
	zassert_mem_equal(orig_buf->data, flat_buffer.data, orig_buf->len);

	net_buf_unref(orig_buf);
}

static struct {
	uint16_t company_code;
	uint8_t payload[10];
} __packed mfg_data;

ZTEST(epacket_bt_adv, test_epacket_detection)
{
	NET_BUF_SIMPLE_DEFINE(flat_buffer, 256);

	struct bt_data flags = BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR));
	struct bt_data not_flags = BT_DATA_BYTES(BT_DATA_TX_POWER, 0x01);
	struct bt_data uuid16 = BT_DATA_BYTES(BT_DATA_UUID16_SOME, 0x00, 0x00);
	struct bt_data not_uuid16 = BT_DATA_BYTES(BT_DATA_NAME_SHORTENED, 'a', '\x00');
	struct bt_data manu = BT_DATA(BT_DATA_MANUFACTURER_DATA, &mfg_data, sizeof(mfg_data));

	/* Not extended advertising */
	net_buf_simple_reset(&flat_buffer);
	zassert_false(epacket_bt_adv_is_epacket(BT_GAP_ADV_TYPE_ADV_IND, &flat_buffer));

	/* First structure not AD flags */
	net_buf_simple_reset(&flat_buffer);
	flat_buffer.len += bt_data_serialize(&uuid16, net_buf_simple_tail(&flat_buffer));
	zassert_false(epacket_bt_adv_is_epacket(BT_GAP_ADV_TYPE_EXT_ADV, &flat_buffer));

	net_buf_simple_reset(&flat_buffer);
	flat_buffer.len += bt_data_serialize(&not_flags, net_buf_simple_tail(&flat_buffer));
	zassert_false(epacket_bt_adv_is_epacket(BT_GAP_ADV_TYPE_EXT_ADV, &flat_buffer));

	/* Second structure not UUID16_SOME */
	net_buf_simple_reset(&flat_buffer);
	flat_buffer.len += bt_data_serialize(&flags, net_buf_simple_tail(&flat_buffer));
	flat_buffer.len += bt_data_serialize(&flags, net_buf_simple_tail(&flat_buffer));
	zassert_false(epacket_bt_adv_is_epacket(BT_GAP_ADV_TYPE_EXT_ADV, &flat_buffer));

	net_buf_simple_reset(&flat_buffer);
	flat_buffer.len += bt_data_serialize(&flags, net_buf_simple_tail(&flat_buffer));
	flat_buffer.len += bt_data_serialize(&not_uuid16, net_buf_simple_tail(&flat_buffer));
	zassert_false(epacket_bt_adv_is_epacket(BT_GAP_ADV_TYPE_EXT_ADV, &flat_buffer));

	/* Third structure not MANUFACTURER_DATA */
	net_buf_simple_reset(&flat_buffer);
	flat_buffer.len += bt_data_serialize(&flags, net_buf_simple_tail(&flat_buffer));
	flat_buffer.len += bt_data_serialize(&uuid16, net_buf_simple_tail(&flat_buffer));
	flat_buffer.len += bt_data_serialize(&flags, net_buf_simple_tail(&flat_buffer));
	zassert_false(epacket_bt_adv_is_epacket(BT_GAP_ADV_TYPE_EXT_ADV, &flat_buffer));

	/* Bad manufacturer ID */
	mfg_data.company_code = 0x1234;
	net_buf_simple_reset(&flat_buffer);
	flat_buffer.len += bt_data_serialize(&flags, net_buf_simple_tail(&flat_buffer));
	flat_buffer.len += bt_data_serialize(&uuid16, net_buf_simple_tail(&flat_buffer));
	flat_buffer.len += bt_data_serialize(&manu, net_buf_simple_tail(&flat_buffer));
	zassert_false(epacket_bt_adv_is_epacket(BT_GAP_ADV_TYPE_EXT_ADV, &flat_buffer));
}

static bool security_init(const void *global_state)
{
	infuse_security_init();
	return true;
}

ZTEST_SUITE(epacket_bt_adv, security_init, NULL, NULL, NULL, NULL);
