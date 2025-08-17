/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>

#include <infuse/epacket/filter.h>
#include <infuse/epacket/packet.h>
#include <infuse/tdf/definitions.h>
#include <infuse/tdf/tdf.h>

ZTEST(epacket_filter, test_auth)
{
	const uint8_t flags = FILTER_FORWARD_ONLY_DECRYPTED;
	struct epacket_rx_metadata *meta;
	struct net_buf *buf;

	buf = epacket_alloc_rx(K_NO_WAIT);
	zassert_not_null(buf);
	meta = net_buf_user_data(buf);

	meta->auth = EPACKET_AUTH_FAILURE;
	zassert_false(epacket_gateway_forward_filter(flags, UINT8_MAX, buf));

	meta->auth = EPACKET_AUTH_REMOTE_ENCRYPTED;
	zassert_false(epacket_gateway_forward_filter(flags, UINT8_MAX, buf));

	meta->auth = EPACKET_AUTH_DEVICE;
	zassert_true(epacket_gateway_forward_filter(flags, UINT8_MAX, buf));

	meta->auth = EPACKET_AUTH_DEVICE;
	zassert_true(epacket_gateway_forward_filter(flags, UINT8_MAX, buf));

	net_buf_unref(buf);
}

ZTEST(epacket_filter, test_tdf)
{
	struct epacket_rx_metadata *meta;
	struct net_buf *buf;

	buf = epacket_alloc_rx(K_NO_WAIT);
	zassert_not_null(buf);
	meta = net_buf_user_data(buf);

	meta->auth = EPACKET_AUTH_DEVICE;
	meta->type = INFUSE_TDF;
	zassert_true(epacket_gateway_forward_filter(FILTER_FORWARD_ONLY_DECRYPTED, UINT8_MAX, buf));

	for (int i = 0; i < UINT8_MAX; i++) {
		if (i == INFUSE_TDF) {
			continue;
		}

		meta->type = i;
		zassert_false(
			epacket_gateway_forward_filter(FILTER_FORWARD_ONLY_TDF, UINT8_MAX, buf));
	}

	net_buf_unref(buf);
}

ZTEST(epacket_filter, test_tdf_announce)
{
	const uint8_t flags = FILTER_FORWARD_ONLY_TDF_ANNOUNCE;
	struct tdf_ambient_temp_pres_hum env = {0};
	struct tdf_announce announce = {0};
	struct tdf_buffer_state buffer_state;
	struct epacket_rx_metadata *meta;
	struct net_buf *buf;

	buf = epacket_alloc_rx(K_NO_WAIT);
	zassert_not_null(buf);
	meta = net_buf_user_data(buf);

	net_buf_simple_init_with_data(&buffer_state.buf, buf->data, buf->size);

	meta->auth = EPACKET_AUTH_FAILURE;
	meta->type = INFUSE_TDF;
	zassert_false(epacket_gateway_forward_filter(flags, UINT8_MAX, buf));

	meta->auth = EPACKET_AUTH_DEVICE;
	meta->type = INFUSE_TDF + 1;
	zassert_false(epacket_gateway_forward_filter(flags, UINT8_MAX, buf));

	/* No TDF payload */
	meta->auth = EPACKET_AUTH_DEVICE;
	meta->type = INFUSE_TDF;
	zassert_false(epacket_gateway_forward_filter(flags, UINT8_MAX, buf));

	/* Not TDF_ANNOUNCE payload */
	tdf_buffer_state_reset(&buffer_state);
	TDF_ADD(&buffer_state, TDF_AMBIENT_TEMP_PRES_HUM, 1, 0, 0, &env);
	buf->len = buffer_state.buf.len;
	zassert_false(epacket_gateway_forward_filter(flags, UINT8_MAX, buf));

	/* TDF_ANNOUNCE payload */
	tdf_buffer_state_reset(&buffer_state);
	TDF_ADD(&buffer_state, TDF_ANNOUNCE, 1, 0, 0, &announce);
	buf->len = buffer_state.buf.len;
	zassert_true(epacket_gateway_forward_filter(flags, UINT8_MAX, buf));

	/* TDF_ANNOUNCE payload after other TDF */
	tdf_buffer_state_reset(&buffer_state);
	TDF_ADD(&buffer_state, TDF_AMBIENT_TEMP_PRES_HUM, 1, 0, 0, &env);
	TDF_ADD(&buffer_state, TDF_ANNOUNCE, 1, 0, 0, &announce);
	buf->len = buffer_state.buf.len;
	zassert_true(epacket_gateway_forward_filter(flags, UINT8_MAX, buf));

	net_buf_unref(buf);
}

ZTEST(epacket_filter, test_tdf_percent)
{
	struct tdf_ambient_temp_pres_hum env = {0};
	struct tdf_announce announce = {0};
	struct tdf_buffer_state buffer_state;
	struct epacket_rx_metadata *meta;
	struct net_buf *buf;
	int cnt;

	buf = epacket_alloc_rx(K_NO_WAIT);
	zassert_not_null(buf);
	meta = net_buf_user_data(buf);
	net_buf_simple_init_with_data(&buffer_state.buf, buf->data, buf->size);

	/* Passes device but 0% chance of passing percent */
	meta->auth = EPACKET_AUTH_DEVICE;
	zassert_false(epacket_gateway_forward_filter(FILTER_FORWARD_ONLY_DECRYPTED, 0, buf));

	/* Passes TDF but 0% chance of passing percent */
	meta->auth = EPACKET_AUTH_DEVICE;
	meta->type = INFUSE_TDF;
	zassert_false(epacket_gateway_forward_filter(FILTER_FORWARD_ONLY_TDF, 0, buf));

	/* Passes TDF_ANNOUNCE but 0% chance of passing percent */
	tdf_buffer_state_reset(&buffer_state);
	TDF_ADD(&buffer_state, TDF_AMBIENT_TEMP_PRES_HUM, 1, 0, 0, &env);
	TDF_ADD(&buffer_state, TDF_ANNOUNCE, 1, 0, 0, &announce);
	buf->len = buffer_state.buf.len;
	zassert_false(epacket_gateway_forward_filter(FILTER_FORWARD_ONLY_TDF_ANNOUNCE, 0, buf));

	/* Check randomness at 25% */
	cnt = 0;
	for (int i = 0; i < 1000; i++) {
		if (epacket_gateway_forward_filter(FILTER_FORWARD_ONLY_TDF_ANNOUNCE, 64, buf)) {
			cnt += 1;
		}
	}
	zassert_within(250, cnt, 50);

	/* Check randomness at 75% */
	cnt = 0;
	for (int i = 0; i < 1000; i++) {
		if (epacket_gateway_forward_filter(FILTER_FORWARD_ONLY_TDF_ANNOUNCE, 192, buf)) {
			cnt += 1;
		}
	}
	zassert_within(750, cnt, 50);

	/* Ensure 100% actually passes all */
	cnt = 0;
	for (int i = 0; i < 10000; i++) {
		if (epacket_gateway_forward_filter(FILTER_FORWARD_ONLY_TDF_ANNOUNCE, UINT8_MAX,
						   buf)) {
			cnt += 1;
		}
	}
	zassert_equal(10000, cnt);

	net_buf_unref(buf);
}

ZTEST_SUITE(epacket_filter, NULL, NULL, NULL, NULL, NULL);
