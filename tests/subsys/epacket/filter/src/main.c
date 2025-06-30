/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/ztest.h>

#include <infuse/epacket/filter.h>
#include <infuse/epacket/packet.h>
#include <infuse/tdf/definitions.h>
#include <infuse/tdf/tdf.h>

ZTEST(epacket_filter, test_auth)
{
	struct epacket_rx_metadata *meta;
	struct net_buf *buf;

	buf = epacket_alloc_rx(K_NO_WAIT);
	zassert_not_null(buf);
	meta = net_buf_user_data(buf);

	meta->auth = EPACKET_AUTH_FAILURE;
	zassert_false(epacket_gateway_forward_filter(FILTER_FORWARD_ONLY_DECRYPTED, buf));

	meta->auth = EPACKET_AUTH_REMOTE_ENCRYPTED;
	zassert_false(epacket_gateway_forward_filter(FILTER_FORWARD_ONLY_DECRYPTED, buf));

	meta->auth = EPACKET_AUTH_DEVICE;
	zassert_true(epacket_gateway_forward_filter(FILTER_FORWARD_ONLY_DECRYPTED, buf));

	meta->auth = EPACKET_AUTH_DEVICE;
	zassert_true(epacket_gateway_forward_filter(FILTER_FORWARD_ONLY_DECRYPTED, buf));

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
	zassert_true(epacket_gateway_forward_filter(FILTER_FORWARD_ONLY_DECRYPTED, buf));

	for (int i = 0; i < UINT8_MAX; i++) {
		if (i == INFUSE_TDF) {
			continue;
		}

		meta->type = i;
		zassert_false(epacket_gateway_forward_filter(FILTER_FORWARD_ONLY_TDF, buf));
	}

	net_buf_unref(buf);
}

ZTEST(epacket_filter, test_tdf_announce)
{
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
	zassert_false(epacket_gateway_forward_filter(FILTER_FORWARD_ONLY_TDF_ANNOUNCE, buf));

	meta->auth = EPACKET_AUTH_DEVICE;
	meta->type = INFUSE_TDF + 1;
	zassert_false(epacket_gateway_forward_filter(FILTER_FORWARD_ONLY_TDF_ANNOUNCE, buf));

	/* No TDF payload */
	meta->auth = EPACKET_AUTH_DEVICE;
	meta->type = INFUSE_TDF;
	zassert_false(epacket_gateway_forward_filter(FILTER_FORWARD_ONLY_TDF_ANNOUNCE, buf));

	/* Not TDF_ANNOUNCE payload */
	tdf_buffer_state_reset(&buffer_state);
	TDF_ADD(&buffer_state, TDF_AMBIENT_TEMP_PRES_HUM, 1, 0, 0, &env);
	buf->len = buffer_state.buf.len;
	zassert_false(epacket_gateway_forward_filter(FILTER_FORWARD_ONLY_TDF_ANNOUNCE, buf));

	/* TDF_ANNOUNCE payload */
	tdf_buffer_state_reset(&buffer_state);
	TDF_ADD(&buffer_state, TDF_ANNOUNCE, 1, 0, 0, &announce);
	buf->len = buffer_state.buf.len;
	zassert_true(epacket_gateway_forward_filter(FILTER_FORWARD_ONLY_TDF_ANNOUNCE, buf));

	/* TDF_ANNOUNCE payload after other TDF */
	tdf_buffer_state_reset(&buffer_state);
	TDF_ADD(&buffer_state, TDF_AMBIENT_TEMP_PRES_HUM, 1, 0, 0, &env);
	TDF_ADD(&buffer_state, TDF_ANNOUNCE, 1, 0, 0, &announce);
	buf->len = buffer_state.buf.len;
	zassert_true(epacket_gateway_forward_filter(FILTER_FORWARD_ONLY_TDF_ANNOUNCE, buf));

	net_buf_unref(buf);
}

ZTEST_SUITE(epacket_filter, NULL, NULL, NULL, NULL, NULL);
