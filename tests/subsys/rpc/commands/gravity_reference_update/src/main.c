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
#include <zephyr/zbus/zbus.h>

#include <infuse/types.h>
#include <infuse/drivers/imu.h>
#include <infuse/rpc/types.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/zbus/channels.h>

IMU_SAMPLE_ARRAY_TYPE_DEFINE(imu_sample_container, 64);

ZBUS_CHAN_DEFINE_WITH_ID(INFUSE_ZBUS_NAME(INFUSE_ZBUS_CHAN_IMU), INFUSE_ZBUS_CHAN_IMU,
			 struct imu_sample_container, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
			 ZBUS_MSG_INIT(0));

#define CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_IMU)

struct imu_sample_container base;

static void send_gravity_reference_update_command(uint32_t request_id, uint16_t max_variance)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_gravity_reference_update_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_GRAVITY_REFERENCE_UPDATE,
			},
		.max_variance = max_variance,
	};

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static struct net_buf *expect_gravity_reference_update_response(uint32_t request_id, int rc)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_gravity_reference_update_response *response;
	struct net_buf *rsp;

	zassert_not_null(response_queue);

	/* Response was sent */
	rsp = k_fifo_get(response_queue, K_SECONDS(15));
	zassert_not_null(rsp);
	net_buf_pull_mem(rsp, sizeof(struct epacket_dummy_frame));
	response = (void *)rsp->data;

	/* Parameters match what we expect */
	zassert_equal(request_id, response->header.request_id);
	zassert_equal(rc, response->header.return_code);

	/* Return the response */
	return rsp;
}

ZTEST(rpc_command_gravity_reference_update, test_data_timeout)
{
	struct net_buf *rsp;

	/* Request with no data being published */
	send_gravity_reference_update_command(100, 0);
	rsp = expect_gravity_reference_update_response(100, -EAGAIN);
	net_buf_unref(rsp);

	/* Request with only one buffer published */
	send_gravity_reference_update_command(101, 0);
	k_sleep(K_MSEC(100));
	zbus_chan_pub(CHAN, &base, K_FOREVER);
	rsp = expect_gravity_reference_update_response(101, -EAGAIN);
	net_buf_unref(rsp);
}

ZTEST(rpc_command_gravity_reference_update, test_data_no_acc)
{
	struct net_buf *rsp;

	base.header.accelerometer.num = 0;

	/* Accelerometer not enabled */
	send_gravity_reference_update_command(102, 0);
	k_sleep(K_MSEC(100));
	zbus_chan_pub(CHAN, &base, K_FOREVER);
	k_sleep(K_MSEC(100));
	zbus_chan_pub(CHAN, &base, K_FOREVER);
	rsp = expect_gravity_reference_update_response(102, -ENODATA);
	net_buf_unref(rsp);
}

static void validate_data(struct rpc_gravity_reference_update_response *response, bool kv_expected)
{
	KV_KEY_TYPE(KV_KEY_GRAVITY_REFERENCE) gravity;

	zassert_equal(base.header.accelerometer.num, response->num_samples);
	zassert_equal(base.header.accelerometer.buffer_period_ticks / 64,
		      response->sample_period_us);

	zassert_equal(105, response->reference.x);
	zassert_equal(-175, response->reference.y);
	zassert_equal(-7950, response->reference.z);
	zassert_equal(26, response->variance.x);
	zassert_equal(635, response->variance.y);
	zassert_equal(2540, response->variance.z);

	if (kv_expected) {
		zassert_equal(sizeof(gravity), KV_STORE_READ(KV_KEY_GRAVITY_REFERENCE, &gravity));
		zassert_equal(105, gravity.x);
		zassert_equal(-175, gravity.y);
		zassert_equal(-7950, gravity.z);
	} else {
		zassert_false(kv_store_key_exists(KV_KEY_GRAVITY_REFERENCE));
	}
}

ZTEST(rpc_command_gravity_reference_update, test_no_max_variance)
{
	struct net_buf *rsp;

	send_gravity_reference_update_command(103, 0);
	k_sleep(K_MSEC(100));
	zbus_chan_pub(CHAN, &base, K_FOREVER);
	k_sleep(K_MSEC(100));
	zbus_chan_pub(CHAN, &base, K_FOREVER);
	rsp = expect_gravity_reference_update_response(103, 6);
	validate_data((void *)rsp->data, true);
	net_buf_unref(rsp);
}

ZTEST(rpc_command_gravity_reference_update, test_variance_ok)
{
	struct net_buf *rsp;

	send_gravity_reference_update_command(104, 4000);
	k_sleep(K_MSEC(100));
	zbus_chan_pub(CHAN, &base, K_FOREVER);
	k_sleep(K_MSEC(100));
	zbus_chan_pub(CHAN, &base, K_FOREVER);
	rsp = expect_gravity_reference_update_response(104, 6);
	validate_data((void *)rsp->data, true);
	net_buf_unref(rsp);
}

ZTEST(rpc_command_gravity_reference_update, test_variance_bad)
{
	struct net_buf *rsp;

	send_gravity_reference_update_command(105, 2000);
	k_sleep(K_MSEC(100));
	zbus_chan_pub(CHAN, &base, K_FOREVER);
	k_sleep(K_MSEC(100));
	zbus_chan_pub(CHAN, &base, K_FOREVER);
	rsp = expect_gravity_reference_update_response(105, -EIO);
	validate_data((void *)rsp->data, false);
	net_buf_unref(rsp);

	send_gravity_reference_update_command(106, 500);
	k_sleep(K_MSEC(100));
	zbus_chan_pub(CHAN, &base, K_FOREVER);
	k_sleep(K_MSEC(100));
	zbus_chan_pub(CHAN, &base, K_FOREVER);
	rsp = expect_gravity_reference_update_response(106, -EIO);
	validate_data((void *)rsp->data, false);
	net_buf_unref(rsp);

	send_gravity_reference_update_command(107, 10);
	k_sleep(K_MSEC(100));
	zbus_chan_pub(CHAN, &base, K_FOREVER);
	k_sleep(K_MSEC(100));
	zbus_chan_pub(CHAN, &base, K_FOREVER);
	rsp = expect_gravity_reference_update_response(107, -EIO);
	validate_data((void *)rsp->data, false);
	net_buf_unref(rsp);
}

static void zbus_before(void *fixture)
{
	for (int i = 0; i < ARRAY_SIZE(base.samples); i++) {
		base.samples[i].x = 100 + 10 * (i % 2);
		base.samples[i].y = -200 + 50 * (i % 2);
		base.samples[i].z = -8000 + 100 * (i % 2);
	}
	base.header.accelerometer.offset = 0;
	base.header.accelerometer.num = ARRAY_SIZE(base.samples);
	base.header.accelerometer.buffer_period_ticks =
		ARRAY_SIZE(base.samples) * k_us_to_ticks_near32(1000);

	/* Reset data to base */
	zbus_chan_pub(CHAN, &base, K_FOREVER);

	/* Delete any stored keys */
	kv_store_delete(KV_KEY_GRAVITY_REFERENCE);
}

ZTEST_SUITE(rpc_command_gravity_reference_update, NULL, NULL, zbus_before, NULL, NULL);
