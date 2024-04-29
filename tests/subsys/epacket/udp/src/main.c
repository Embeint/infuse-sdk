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

uint32_t infuse_device_id(void)
{
	return 0x123456;
}

ZTEST(epacket_udp, test_encrypt_decrypt)
{
	const struct device *epacket_udp = DEVICE_DT_GET(DT_NODELABEL(epacket_udp));
	struct net_buf *orig_buf, *encr_buf, *copy_buf;
	uint8_t *p;
	int rc;

	/* Create original buffer */
	orig_buf = epacket_alloc_tx_for_interface(epacket_udp, K_NO_WAIT);
	zassert_not_null(orig_buf);
	epacket_set_tx_metadata(orig_buf, EPACKET_AUTH_DEVICE, 0, 0x10);
	p = net_buf_add(orig_buf, 60);
	sys_rand_get(p, 60);

	/* Encrypt original buffer */
	encr_buf = net_buf_clone(orig_buf, K_NO_WAIT);
	zassert_not_null(encr_buf);
	rc = epacket_udp_encrypt(encr_buf);
	zassert_equal(0, rc);
	zassert_equal(orig_buf->len + EPACKET_UDP_FRAME_EXPECTED_SIZE + 16, encr_buf->len);

	/* Decrypt unmodified packet */
	copy_buf = net_buf_clone(encr_buf, K_NO_WAIT);
	zassert_not_null(copy_buf);
	rc = epacket_udp_decrypt(copy_buf);
	zassert_equal(0, rc);
	zassert_equal(orig_buf->len, copy_buf->len);
	zassert_mem_equal(orig_buf->data, copy_buf->data, copy_buf->len);
	net_buf_unref(copy_buf);

	/* Test failure on modified encryption buffer */
	for (int i = 0; i < encr_buf->len; i++) {
		copy_buf = net_buf_clone(encr_buf, K_NO_WAIT);
		zassert_not_null(copy_buf);
		copy_buf->data[i]++;
		rc = epacket_udp_decrypt(copy_buf);
		zassert_equal(-1, rc);
		net_buf_unref(copy_buf);
	}
}

static bool psa_init(const void *global_state)
{
	zassert_equal(PSA_SUCCESS, psa_crypto_init());
	return true;
}

ZTEST_SUITE(epacket_udp, psa_init, NULL, NULL, NULL, NULL);
