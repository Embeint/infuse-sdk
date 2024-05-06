/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <infuse/epacket/keys.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_serial.h>

#include "../subsys/epacket/interfaces/epacket_internal.h"

#define SYNC_A 0xD5
#define SYNC_B 0xCA

struct serial_header {
	uint8_t sync[2];
	uint16_t len;
} __packed;

static K_FIFO_DEFINE(packet_queue);

uint64_t infuse_device_id(void)
{
	return 0x0123456789ABCDEF;
}

void receive_handler(struct net_buf *buf)
{
	k_fifo_put(&packet_queue, buf);
}

ZTEST(epacket_serial, test_reconstructor)
{
	struct serial_header s = {
		.sync = {SYNC_A, SYNC_B},
	};
	uint8_t buffer[64];
	struct net_buf *out;

	/* Valid packet 1 */
	s.len = 10;
	epacket_serial_reconstruct(NULL, (void *)&s, sizeof(s), receive_handler);
	zassert_is_null(k_fifo_get(&packet_queue, K_MSEC(10)));
	epacket_serial_reconstruct(NULL, buffer, 9, receive_handler);
	zassert_is_null(k_fifo_get(&packet_queue, K_MSEC(10)));
	epacket_serial_reconstruct(NULL, buffer, 1, receive_handler);
	out = k_fifo_get(&packet_queue, K_MSEC(10));
	zassert_not_null(out);
	net_buf_unref(out);

	/* Valid packet 2 */
	s.len = 4;
	epacket_serial_reconstruct(NULL, (void *)&s, sizeof(s), receive_handler);
	zassert_is_null(k_fifo_get(&packet_queue, K_MSEC(10)));
	epacket_serial_reconstruct(NULL, buffer, 3, receive_handler);
	zassert_is_null(k_fifo_get(&packet_queue, K_MSEC(10)));
	epacket_serial_reconstruct(NULL, buffer, 1, receive_handler);
	out = k_fifo_get(&packet_queue, K_MSEC(10));
	zassert_not_null(out);
	net_buf_unref(out);

	/* Random junk ascii */
	for (int i = 0; i < 128; i++) {
		char c = ' ';
		char d = '~' - ' ';

		c += sys_rand32_get() % d;
		epacket_serial_reconstruct(NULL, &c, 1, receive_handler);
		zassert_is_null(k_fifo_get(&packet_queue, K_MSEC(10)));
	}

	/* Valid packet 3 */
	s.len = 30;
	epacket_serial_reconstruct(NULL, (void *)&s, sizeof(s), receive_handler);
	zassert_is_null(k_fifo_get(&packet_queue, K_MSEC(10)));
	epacket_serial_reconstruct(NULL, buffer, 29, receive_handler);
	zassert_is_null(k_fifo_get(&packet_queue, K_MSEC(10)));
	epacket_serial_reconstruct(NULL, buffer, 1, receive_handler);
	out = k_fifo_get(&packet_queue, K_MSEC(10));
	zassert_not_null(out);
	net_buf_unref(out);

	/* Bad sync bytes */
	s.sync[0] += 1;
	epacket_serial_reconstruct(NULL, (void *)&s, sizeof(s), receive_handler);
	epacket_serial_reconstruct(NULL, buffer, sizeof(buffer), receive_handler);
	out = k_fifo_get(&packet_queue, K_MSEC(10));
	zassert_is_null(out);

	s.sync[0] -= 1;
	s.sync[1] += 1;
	epacket_serial_reconstruct(NULL, (void *)&s, sizeof(s), receive_handler);
	epacket_serial_reconstruct(NULL, buffer, sizeof(buffer), receive_handler);
	out = k_fifo_get(&packet_queue, K_MSEC(10));
	zassert_is_null(out);
}

ZTEST(epacket_serial, test_reconstructor_zero)
{
	struct serial_header s = {
		.sync = {SYNC_A, SYNC_B},
		.len = 0,
	};
	struct net_buf *out;

	epacket_serial_reconstruct(NULL, (void *)&s, sizeof(s), receive_handler);
	out = k_fifo_get(&packet_queue, K_MSEC(10));
	zassert_not_null(out);
	net_buf_unref(out);

	epacket_serial_reconstruct(NULL, (void *)&s, sizeof(s), receive_handler);
	out = k_fifo_get(&packet_queue, K_MSEC(10));
	zassert_not_null(out);
	net_buf_unref(out);

	epacket_serial_reconstruct(NULL, (void *)&s, sizeof(s), receive_handler);
	out = k_fifo_get(&packet_queue, K_MSEC(10));
	zassert_not_null(out);
	net_buf_unref(out);
}

ZTEST(epacket_serial, test_reconstructor_too_large)
{
	struct serial_header s = {
		.sync = {SYNC_A, SYNC_B},
		.len = CONFIG_EPACKET_PACKET_SIZE_MAX + 1,
	};
	uint8_t buffer[CONFIG_EPACKET_PACKET_SIZE_MAX + 1];

	epacket_serial_reconstruct(NULL, (void *)&s, sizeof(s), receive_handler);
	epacket_serial_reconstruct(NULL, (void *)buffer, sizeof(buffer), receive_handler);
	/* Too large packet should be dropped */
	zassert_is_null(k_fifo_get(&packet_queue, K_MSEC(10)));
}

ZTEST(epacket_serial, test_decrypt_error)
{
	struct net_buf *rx;
	uint8_t payload[64];

	for (int i = 1; i <= sizeof(struct epacket_serial_frame) + 16; i++) {
		/* Create too small buffer */
		rx = epacket_alloc_rx(K_NO_WAIT);
		zassert_not_null(rx);
		net_buf_add_mem(rx, payload, i);

		/* Ensure decode errors */
		zassert_equal(-1, epacket_serial_decrypt(rx));
		net_buf_unref(rx);
	}
}

ZTEST(epacket_serial, test_sequence)
{
	struct epacket_rx_metadata *meta;
	enum epacket_auth iter_auth;
	struct net_buf *tx, *rx;
	uint16_t extra_flags, seqs[8];
	uint8_t *p;
	int rc;

	rx = epacket_alloc_rx(K_NO_WAIT);
	zassert_not_null(rx);

	for (int i = 0; i < ARRAY_SIZE(seqs); i++) {
		iter_auth = i % 2 ? EPACKET_AUTH_DEVICE : EPACKET_AUTH_NETWORK;
		extra_flags = i % 2 ? EPACKET_FLAGS_ENCRYPTION_DEVICE : EPACKET_FLAGS_ENCRYPTION_NETWORK;
		/* Construct buffer */
		tx = epacket_alloc_tx(K_NO_WAIT);
		zassert_not_null(tx);
		net_buf_reserve(tx, sizeof(struct epacket_serial_frame));
		epacket_set_tx_metadata(tx, iter_auth, i, 0x10 + i);
		p = net_buf_add(tx, 60);
		sys_rand_get(p, 60);

		/* Encrypt payload */
		rc = epacket_serial_encrypt(tx);
		zassert_equal(0, rc);

		/* Copy message contents across to RX buffer */
		net_buf_reset(rx);
		net_buf_add_mem(rx, tx->data, tx->len);
		net_buf_unref(tx);

		/* Decrypt and free */
		rc = epacket_serial_decrypt(rx);
		zassert_equal(0, rc);
		meta = net_buf_user_data(rx);
		zassert_equal(iter_auth, meta->auth);
		zassert_equal(0x10 + i, meta->type);
		zassert_equal(extra_flags | i, meta->flags);
		seqs[i] = meta->sequence;

		if (i > 0) {
			/* Sequence number should increase on each packet */
			zassert_equal(seqs[i - 1] + 1, seqs[i]);
		}
	}
	net_buf_unref(rx);
}

ZTEST(epacket_serial, test_encrypt_decrypt)
{
	struct net_buf *orig_buf, *encr_buf, *rx, *rx_copy_buf, *in;
	struct epacket_rx_metadata *meta;
	uint8_t *p;
	int rc;

	/* Create original buffer */
	orig_buf = epacket_alloc_tx(K_NO_WAIT);
	zassert_not_null(orig_buf);
	net_buf_reserve(orig_buf, sizeof(struct epacket_serial_frame));
	epacket_set_tx_metadata(orig_buf, EPACKET_AUTH_DEVICE, 0, 0x10);
	p = net_buf_add(orig_buf, 60);
	sys_rand_get(p, 60);

	/* Encrypt original buffer */
	encr_buf = net_buf_clone(orig_buf, K_NO_WAIT);
	zassert_not_null(encr_buf);
	rc = epacket_serial_encrypt(encr_buf);
	zassert_equal(0, rc);
	zassert_equal(orig_buf->len + sizeof(struct epacket_serial_frame) + 16, encr_buf->len);

	/* Copy message contents across to RX buffer */
	rx = epacket_alloc_rx(K_NO_WAIT);
	zassert_not_null(rx);
	net_buf_add_mem(rx, encr_buf->data, encr_buf->len);
	net_buf_unref(encr_buf);

	/* Decrypt unmodified packet */
	rx_copy_buf = net_buf_clone(rx, K_NO_WAIT);
	zassert_not_null(rx_copy_buf);
	rc = epacket_serial_decrypt(rx_copy_buf);
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
		rc = epacket_serial_decrypt(rx_copy_buf);
		meta = net_buf_user_data(rx_copy_buf);
		zassert_equal(-1, rc);
		zassert_equal(EPACKET_AUTH_FAILURE, meta->auth);
		zassert_equal(in->len, rx_copy_buf->len);
		zassert_mem_equal(in->data, rx_copy_buf->data, in->len);
		net_buf_unref(rx_copy_buf);
		net_buf_unref(in);
	}
	net_buf_unref(rx);
}

static bool psa_init(const void *global_state)
{
	zassert_equal(PSA_SUCCESS, psa_crypto_init());
	return true;
}

ZTEST_SUITE(epacket_serial, psa_init, NULL, NULL, NULL, NULL);
