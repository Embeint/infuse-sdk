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

void receive_handler(struct epacket_receive_metadata *metadata, struct net_buf *buf)
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

	/* Valid packet 2 */
	s.len = 4;
	epacket_serial_reconstruct(NULL, (void *)&s, sizeof(s), receive_handler);
	zassert_is_null(k_fifo_get(&packet_queue, K_MSEC(10)));
	epacket_serial_reconstruct(NULL, buffer, 3, receive_handler);
	zassert_is_null(k_fifo_get(&packet_queue, K_MSEC(10)));
	epacket_serial_reconstruct(NULL, buffer, 1, receive_handler);
	out = k_fifo_get(&packet_queue, K_MSEC(10));
	zassert_not_null(out);

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

ZTEST(epacket_serial, test_sequence)
{
	struct net_buf *buf;
	uint16_t seqs[8];
	uint8_t *p;
	int rc;

	for (int i = 0; i < ARRAY_SIZE(seqs); i++) {
		/* Construct buffer */
		buf = epacket_alloc_tx(K_NO_WAIT);
		zassert_not_null(buf);
		net_buf_reserve(buf, EPACKET_SERIAL_FRAME_EXPECTED_SIZE);
		epacket_set_tx_metadata(buf, EPACKET_AUTH_DEVICE, 0, 0x10);
		p = net_buf_add(buf, 60);
		sys_rand_get(p, 60);

		/* Encrypt payload */
		rc = epacket_serial_encrypt(buf);
		zassert_equal(0, rc);

		/* Decrypt and free */
		rc = epacket_serial_decrypt(buf, &seqs[i]);
		zassert_equal(0, rc);
		net_buf_unref(buf);

		if (i > 0) {
			/* Sequence number should increase on each packet */
			zassert_equal(seqs[i - 1] + 1, seqs[i]);
		}
	}
}

ZTEST(epacket_serial, test_encrypt_decrypt)
{
	struct net_buf *orig_buf, *encr_buf, *copy_buf;
	uint16_t seq;
	uint8_t *p;
	int rc;

	/* Create original buffer */
	orig_buf = epacket_alloc_tx(K_NO_WAIT);
	zassert_not_null(orig_buf);
	net_buf_reserve(orig_buf, EPACKET_SERIAL_FRAME_EXPECTED_SIZE);
	epacket_set_tx_metadata(orig_buf, EPACKET_AUTH_DEVICE, 0, 0x10);
	p = net_buf_add(orig_buf, 60);
	sys_rand_get(p, 60);

	/* Encrypt original buffer */
	encr_buf = net_buf_clone(orig_buf, K_NO_WAIT);
	zassert_not_null(encr_buf);
	rc = epacket_serial_encrypt(encr_buf);
	zassert_equal(0, rc);
	zassert_equal(orig_buf->len + EPACKET_SERIAL_FRAME_EXPECTED_SIZE + 16, encr_buf->len);

	/* Decrypt unmodified packet */
	copy_buf = net_buf_clone(encr_buf, K_NO_WAIT);
	zassert_not_null(copy_buf);
	rc = epacket_serial_decrypt(copy_buf, &seq);
	zassert_equal(0, rc);
	zassert_equal(orig_buf->len, copy_buf->len);
	zassert_mem_equal(orig_buf->data, copy_buf->data, copy_buf->len);
	net_buf_unref(copy_buf);

	/* Test failure on modified encryption buffer */
	for (int i = 0; i < encr_buf->len; i++) {
		copy_buf = net_buf_clone(encr_buf, K_NO_WAIT);
		zassert_not_null(copy_buf);
		copy_buf->data[i]++;
		rc = epacket_serial_decrypt(copy_buf, &seq);
		zassert_equal(-1, rc);
		net_buf_unref(copy_buf);
	}
}

static bool psa_init(const void *global_state)
{
	zassert_equal(PSA_SUCCESS, psa_crypto_init());
	return true;
}

ZTEST_SUITE(epacket_serial, psa_init, NULL, NULL, NULL, NULL);
