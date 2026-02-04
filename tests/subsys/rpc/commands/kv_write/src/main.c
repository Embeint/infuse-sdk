/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/common_boot.h>
#include <infuse/types.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/rpc/commands/kv_write.h>
#include <infuse/rpc/types.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>

static void send_kv_write_command(uint32_t request_id, struct net_buf_simple *values, uint8_t num)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_kv_write_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_KV_WRITE,
			},
		.num = num,
	};

	/* Push command at RPC server */
	epacket_dummy_receive_extra(epacket_dummy, &header, &params, sizeof(params), values->data,
				    values->len);
}

static struct net_buf *expect_kv_write_response(uint32_t request_id, int16_t rc,
						uint8_t expected_responses)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_kv_write_response *response;
	struct net_buf *rsp;
	uint8_t actual_responses;

	zassert_not_null(response_queue);

	/* Response was sent */
	rsp = k_fifo_get(response_queue, K_MSEC(100));
	zassert_not_null(rsp);
	net_buf_pull_mem(rsp, sizeof(struct epacket_dummy_frame));
	response = (void *)rsp->data;

	/* Parameters match what we expect */
	zassert_equal(request_id, response->header.request_id);
	zassert_equal(rc, response->header.return_code);
	actual_responses = (rsp->len - sizeof(struct rpc_kv_write_response)) / sizeof(int16_t);
	zassert_equal(expected_responses, actual_responses);

	/* Return the response */
	return rsp;
}

ZTEST(rpc_command_kv_write, test_kv_write_bad_input)
{
	NET_BUF_SIMPLE_DEFINE(values, 128);
	struct rpc_struct_kv_store_value *value;
	struct net_buf *rsp;

	(void)kv_store_delete(KV_KEY_REBOOTS);

	/* Data not present */
	net_buf_simple_reset(&values);
	value = net_buf_simple_add(&values, sizeof(*value));
	value->id = KV_KEY_REBOOTS;
	value->len = sizeof(uint32_t);

	send_kv_write_command(5, &values, 1);
	rsp = expect_kv_write_response(5, -EINVAL, 0);
	net_buf_unref(rsp);
}

ZTEST(rpc_command_kv_write, test_kv_write_read_only)
{
	NET_BUF_SIMPLE_DEFINE(values, 128);
	struct rpc_struct_kv_store_value *value;
	struct rpc_kv_write_response *response;
	struct net_buf *rsp;

	(void)kv_store_delete(KV_KEY_LTE_SIM_UICC);

	/* Data not present */
	net_buf_simple_reset(&values);
	value = net_buf_simple_add(&values, sizeof(*value));
	value->id = KV_KEY_LTE_SIM_UICC;
	value->len = sizeof(uint32_t);
	net_buf_simple_add_le32(&values, 542);

	send_kv_write_command(6, &values, 1);
	rsp = expect_kv_write_response(6, 0, 1);
	response = (void *)rsp->data;
	zassert_equal(-EPERM, response->rc[0]);
	net_buf_unref(rsp);
}

ZTEST(rpc_command_kv_write, test_kv_write_single)
{
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots;
	NET_BUF_SIMPLE_DEFINE(values, 128);
	struct rpc_struct_kv_store_value *value;
	struct rpc_kv_write_response *response;
	struct net_buf *rsp;
	int rc;

	(void)kv_store_delete(KV_KEY_REBOOTS);

	/* Write a single key that does not yet exist */
	net_buf_simple_reset(&values);
	value = net_buf_simple_add(&values, sizeof(*value));
	value->id = KV_KEY_REBOOTS;
	value->len = sizeof(uint32_t);
	net_buf_simple_add_le32(&values, 542);

	send_kv_write_command(1, &values, 1);
	rsp = expect_kv_write_response(1, 0, 1);
	response = (void *)rsp->data;
	zassert_equal(sizeof(uint32_t), response->rc[0]);
	net_buf_unref(rsp);

	/* Read back from KV store */
	rc = KV_STORE_READ(KV_KEY_REBOOTS, &reboots);
	zassert_equal(sizeof(uint32_t), rc);
	zassert_equal(542, reboots.count);

	/* Write the same value again */
	net_buf_simple_reset(&values);
	value = net_buf_simple_add(&values, sizeof(*value));
	value->id = KV_KEY_REBOOTS;
	value->len = sizeof(uint32_t);
	net_buf_simple_add_le32(&values, 542);

	send_kv_write_command(2, &values, 1);
	rsp = expect_kv_write_response(2, 0, 1);
	response = (void *)rsp->data;
	zassert_equal(0, response->rc[0]);
	net_buf_unref(rsp);

	/* Read back from KV store */
	rc = KV_STORE_READ(KV_KEY_REBOOTS, &reboots);
	zassert_equal(sizeof(uint32_t), rc);
	zassert_equal(542, reboots.count);

	/* Write to a disabled key */
	net_buf_simple_reset(&values);
	value = net_buf_simple_add(&values, sizeof(*value));
	value->id = 0x123A;
	value->len = sizeof(uint32_t);
	net_buf_simple_add_le32(&values, 542);

	send_kv_write_command(3, &values, 1);
	rsp = expect_kv_write_response(3, 0, 1);
	response = (void *)rsp->data;
	zassert_equal(-EACCES, response->rc[0]);
	net_buf_unref(rsp);
}

ZTEST(rpc_command_kv_write, test_kv_write_delete)
{
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots;
	NET_BUF_SIMPLE_DEFINE(values, 128);
	struct rpc_struct_kv_store_value *value;
	struct rpc_kv_write_response *response;
	struct net_buf *rsp;
	int rc;

	reboots.count = 10;
	zassert_equal(sizeof(reboots), KV_STORE_WRITE(KV_KEY_REBOOTS, &reboots));

	/* Delete a key */
	net_buf_simple_reset(&values);
	value = net_buf_simple_add(&values, sizeof(*value));
	value->id = KV_KEY_REBOOTS;
	value->len = 0;

	send_kv_write_command(3, &values, 1);
	rsp = expect_kv_write_response(3, 0, 1);
	response = (void *)rsp->data;
	zassert_equal(0, response->rc[0]);
	net_buf_unref(rsp);

	rc = KV_STORE_READ(KV_KEY_REBOOTS, &reboots);
	zassert_equal(-ENOENT, rc);
}

ZTEST(rpc_command_kv_write, test_kv_write_multi)
{
	KV_STRING_CONST(test_string, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
	KV_KEY_TYPE_VAR(KV_KEY_WIFI_SSID, 64) ssid;
	KV_KEY_TYPE(KV_KEY_FIXED_LOCATION) location = {0};
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots;
	NET_BUF_SIMPLE_DEFINE(values, 128);
	struct rpc_struct_kv_store_value *value;
	struct rpc_kv_write_response *response;
	struct net_buf *rsp;
	int rc;

	(void)kv_store_delete(KV_KEY_REBOOTS);
	(void)kv_store_delete(KV_KEY_WIFI_SSID);
	zassert_equal(sizeof(location), KV_STORE_WRITE(KV_KEY_FIXED_LOCATION, &location));

	/* Write two keys that exist, delete a key in the middle */
	net_buf_simple_reset(&values);
	value = net_buf_simple_add(&values, sizeof(*value));
	value->id = KV_KEY_REBOOTS;
	value->len = sizeof(uint32_t);
	net_buf_simple_add_le32(&values, 542);
	value = net_buf_simple_add(&values, sizeof(*value));
	value->id = KV_KEY_FIXED_LOCATION;
	value->len = 0;
	value = net_buf_simple_add(&values, sizeof(*value));
	value->id = KV_KEY_WIFI_SSID;
	value->len = sizeof(test_string);
	net_buf_simple_add_mem(&values, &test_string, sizeof(test_string));

	send_kv_write_command(1, &values, 3);
	rsp = expect_kv_write_response(1, 0, 3);
	response = (void *)rsp->data;
	zassert_equal(sizeof(uint32_t), response->rc[0]);
	zassert_equal(0, response->rc[1]);
	zassert_equal(sizeof(test_string), response->rc[2]);
	net_buf_unref(rsp);

	/* Read back from KV store */
	rc = KV_STORE_READ(KV_KEY_REBOOTS, &reboots);
	zassert_equal(sizeof(uint32_t), rc);
	zassert_equal(542, reboots.count);
	rc = KV_STORE_READ(KV_KEY_FIXED_LOCATION, &location);
	zassert_equal(-ENOENT, rc);
	rc = KV_STORE_READ(KV_KEY_WIFI_SSID, &ssid);
	zassert_equal(sizeof(test_string), rc);
	zassert_mem_equal(&test_string, &ssid, sizeof(test_string));
}

static int expected_key;
static void *expected_data;
static size_t expected_len;
static bool write_allowed;

bool infuse_rpc_command_kv_write_validate(struct epacket_rx_metadata *meta, uint16_t key,
					  const void *data, size_t len)
{
	zassert_not_null(meta);
	zassert_equal(EPACKET_INTERFACE_DUMMY, meta->interface_id);
	zassert_equal(EPACKET_AUTH_DEVICE, meta->auth);

	if (expected_key == -1) {
		/* Other tests running */
		return true;
	}

	zassert_equal(expected_key, key);
	zassert_equal(expected_len, len);
	if (len > 0) {
		zassert_not_null(expected_data);
		zassert_not_null(data);
		/* Asserted above, but compiler can't tell and complains */
		if (expected_data && data) {
			zassert_mem_equal(expected_data, data, len);
		}
	} else {
		zassert_is_null(expected_data);
		zassert_is_null(data);
	}

	return write_allowed;
}

ZTEST(rpc_command_kv_write, test_kv_write_app_validation)
{
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots;
	NET_BUF_SIMPLE_DEFINE(values, 128);
	struct rpc_struct_kv_store_value *value;
	struct rpc_kv_write_response *response;
	struct net_buf *rsp;

	if (!IS_ENABLED(CONFIG_INFUSE_RPC_OPTION_KV_WRITE_APP_VALIDATE)) {
		ztest_test_skip();
		return;
	}

	(void)kv_store_delete(KV_KEY_REBOOTS);

	/* Write that is not allowed (return code, no value written) */
	net_buf_simple_reset(&values);
	value = net_buf_simple_add(&values, sizeof(*value));
	value->id = KV_KEY_REBOOTS;
	value->len = sizeof(reboots);
	net_buf_simple_add_mem(&values, &reboots, sizeof(reboots));

	expected_key = KV_KEY_REBOOTS;
	expected_data = &reboots;
	expected_len = sizeof(reboots);
	write_allowed = false;

	send_kv_write_command(1, &values, 1);
	rsp = expect_kv_write_response(1, 0, 1);
	response = (void *)rsp->data;
	zassert_equal(-EINVAL, response->rc[0]);
	net_buf_unref(rsp);
	zassert_false(kv_store_key_exists(KV_KEY_REBOOTS));

	/* Write now allowed */
	write_allowed = true;
	send_kv_write_command(2, &values, 1);
	rsp = expect_kv_write_response(2, 0, 1);
	response = (void *)rsp->data;
	zassert_equal(sizeof(reboots), response->rc[0]);
	net_buf_unref(rsp);
	zassert_true(kv_store_key_exists(KV_KEY_REBOOTS));

	/* Delete that is not allowed */
	value->len = 0;
	expected_data = NULL;
	expected_len = 0;
	write_allowed = false;

	send_kv_write_command(3, &values, 1);
	rsp = expect_kv_write_response(3, 0, 1);
	response = (void *)rsp->data;
	zassert_equal(-EINVAL, response->rc[0]);
	net_buf_unref(rsp);
	zassert_true(kv_store_key_exists(KV_KEY_REBOOTS));

	/* Delete now allowed */
	write_allowed = true;

	send_kv_write_command(4, &values, 1);
	rsp = expect_kv_write_response(4, 0, 1);
	response = (void *)rsp->data;
	zassert_equal(0, response->rc[0]);
	net_buf_unref(rsp);
	zassert_false(kv_store_key_exists(KV_KEY_REBOOTS));
}

static void test_before(void *fixture)
{
	/* Reset validation state */
	expected_key = -1;
	write_allowed = true;
}

ZTEST_SUITE(rpc_command_kv_write, NULL, NULL, test_before, NULL, NULL);
