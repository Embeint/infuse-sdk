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
#include <infuse/epacket/interface/epacket_udp.h>

#include "../subsys/epacket/interfaces/epacket_internal.h"

static K_FIFO_DEFINE(packet_queue);

uint64_t infuse_device_id(void)
{
	return 0x0123456789ABCDEF;
}

ZTEST(epacket_udp, test_metadata)
{
	struct epacket_rx_metadata *meta;
	struct net_buf *tx, *rx;
	uint16_t seqs[8];
	uint8_t *p;
	int rc;

	rx = epacket_alloc_rx(K_NO_WAIT);
	zassert_not_null(rx);

	for (int i = 0; i < ARRAY_SIZE(seqs); i++) {
		/* Construct buffer */
		tx = epacket_alloc_tx(K_NO_WAIT);
		zassert_not_null(tx);
		net_buf_reserve(tx, sizeof(struct epacket_udp_frame));
		epacket_set_tx_metadata(tx, EPACKET_AUTH_DEVICE, i, 0x10 + i);
		p = net_buf_add(tx, 60);
		sys_rand_get(p, 60);

		/* Encrypt payload */
		rc = epacket_udp_encrypt(tx);
		zassert_equal(0, rc);

		/* Copy message contents across to RX buffer */
		net_buf_reset(rx);
		net_buf_add_mem(rx, tx->data, tx->len);
		net_buf_unref(tx);

		/* Decrypt and free */
		rc = epacket_udp_decrypt(rx);
		zassert_equal(0, rc);
		meta = net_buf_user_data(rx);
		zassert_equal(EPACKET_AUTH_DEVICE, meta->auth);
		zassert_equal(0x10 + i, meta->type);
		zassert_equal(EPACKET_FLAGS_ENCRYPTION_DEVICE | i, meta->flags);
		seqs[i] = meta->sequence;

		if (i > 0) {
			/* Sequence number should increase on each packet */
			zassert_equal(seqs[i - 1] + 1, seqs[i]);
		}
	}
	net_buf_unref(rx);
}

ZTEST(epacket_udp, test_decrypt_error)
{
	struct net_buf *rx;
	uint8_t payload[64];

	for (int i = 1; i <= sizeof(struct epacket_udp_frame) + 16; i++) {
		/* Create too small buffer */
		rx = epacket_alloc_rx(K_NO_WAIT);
		zassert_not_null(rx);
		net_buf_add_mem(rx, payload, i);

		/* Ensure decode errors */
		zassert_equal(-1, epacket_udp_decrypt(rx));
		net_buf_unref(rx);
	}
}

ZTEST(epacket_udp, test_encrypt_decrypt)
{
	struct net_buf *orig_buf, *encr_buf, *rx, *rx_copy_buf, *in;
	struct epacket_rx_metadata *meta;
	uint8_t *p;
	int rc;

	/* Create original buffer */
	orig_buf = epacket_alloc_tx(K_NO_WAIT);
	zassert_not_null(orig_buf);
	net_buf_reserve(orig_buf, sizeof(struct epacket_udp_frame));
	epacket_set_tx_metadata(orig_buf, EPACKET_AUTH_DEVICE, 0, 0x10);
	p = net_buf_add(orig_buf, 60);
	sys_rand_get(p, 60);

	/* Encrypt original buffer */
	encr_buf = net_buf_clone(orig_buf, K_NO_WAIT);
	zassert_not_null(encr_buf);
	rc = epacket_udp_encrypt(encr_buf);
	zassert_equal(0, rc);
	zassert_equal(orig_buf->len + sizeof(struct epacket_udp_frame) + 16, encr_buf->len);

	/* Copy message contents across to RX buffer */
	rx = epacket_alloc_rx(K_NO_WAIT);
	zassert_not_null(rx);
	net_buf_add_mem(rx, encr_buf->data, encr_buf->len);
	net_buf_unref(encr_buf);

	/* Decrypt unmodified packet */
	rx_copy_buf = net_buf_clone(rx, K_NO_WAIT);
	zassert_not_null(rx_copy_buf);
	rc = epacket_udp_decrypt(rx_copy_buf);
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
		rc = epacket_udp_decrypt(rx_copy_buf);
		meta = net_buf_user_data(rx_copy_buf);
		zassert_equal(-1, rc);
		zassert_equal(EPACKET_AUTH_FAILURE, meta->auth);
		zassert_equal(in->len, rx_copy_buf->len);
		zassert_mem_equal(in->data, rx_copy_buf->data, in->len);
		net_buf_unref(rx_copy_buf);
		net_buf_unref(in);
	}
}

static bool psa_init(const void *global_state)
{
	zassert_equal(PSA_SUCCESS, psa_crypto_init());
	return true;
}

ZTEST_SUITE(epacket_udp, psa_init, NULL, NULL, NULL, NULL);
