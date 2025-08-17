/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/types.h>
#include <infuse/rpc/types.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/zbus/channels.h>

struct large {
	uint8_t val[128];
};

INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_BATTERY);
INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_AMBIENT_ENV);
ZBUS_CHAN_DEFINE_WITH_ID(large_channel, 100, struct large, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
			 ZBUS_MSG_INIT(0));

static void send_zbus_channel_state_command(uint32_t request_id, uint32_t channel_id)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_zbus_channel_state_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_ZBUS_CHANNEL_STATE,
			},
		.channel_id = channel_id,
	};

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static struct net_buf *expect_zbus_channel_state_response(uint32_t request_id, int rc)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_zbus_channel_state_response *response;
	struct net_buf *rsp;

	zassert_not_null(response_queue);

	/* Response was sent */
	rsp = k_fifo_get(response_queue, K_MSEC(100));
	zassert_not_null(rsp);
	net_buf_pull_mem(rsp, sizeof(struct epacket_dummy_frame));
	response = (void *)rsp->data;

	/* Parameters match what we expect */
	zassert_equal(request_id, response->header.request_id);
	zassert_equal(rc, response->header.return_code);

	/* Return the response */
	return rsp;
}

ZTEST(rpc_command_zbus_channel_state, test_bad_channel_id)
{
	struct net_buf *rsp;

	send_zbus_channel_state_command(1000, ZBUS_CHAN_ID_INVALID);
	rsp = expect_zbus_channel_state_response(1000, -EBADF);
	net_buf_unref(rsp);

	send_zbus_channel_state_command(1001, 0x1234567);
	rsp = expect_zbus_channel_state_response(1001, -EBADF);
	net_buf_unref(rsp);
}

ZTEST(rpc_command_zbus_channel_state, test_not_yet_published)
{
	struct rpc_zbus_channel_state_response *response;
	struct net_buf *rsp;

	send_zbus_channel_state_command(1002, INFUSE_ZBUS_CHAN_BATTERY);
	rsp = expect_zbus_channel_state_response(1002, -EAGAIN);
	response = (void *)rsp->data;
	zassert_equal(0, rsp->len - sizeof(*response));
	net_buf_unref(rsp);

	send_zbus_channel_state_command(1003, INFUSE_ZBUS_CHAN_AMBIENT_ENV);
	rsp = expect_zbus_channel_state_response(1003, -EAGAIN);
	response = (void *)rsp->data;
	zassert_equal(0, rsp->len - sizeof(*response));
	net_buf_unref(rsp);
}

ZTEST(rpc_command_zbus_channel_state, test_data_retrieval)
{
	struct tdf_battery_state battery = {0};
	struct tdf_ambient_temp_pres_hum ambient = {0};
	struct rpc_zbus_channel_state_response *response;
	struct net_buf *rsp;

	/* Ensure we don't publish at T=0 */
	k_sleep(K_MSEC(100));

	zbus_chan_pub(INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY), &battery, K_FOREVER);

	send_zbus_channel_state_command(1004, INFUSE_ZBUS_CHAN_BATTERY);
	rsp = expect_zbus_channel_state_response(1004, 0);
	response = (void *)rsp->data;
	zassert_equal(1, response->publish_count);
	zassert_not_equal(0, response->publish_timestamp);
	zassert_not_equal(0, response->publish_period_avg_ms);
	zassert_equal(sizeof(battery), rsp->len - sizeof(*response));
	net_buf_unref(rsp);

	zbus_chan_pub(INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_AMBIENT_ENV), &ambient, K_FOREVER);

	send_zbus_channel_state_command(1005, INFUSE_ZBUS_CHAN_AMBIENT_ENV);
	rsp = expect_zbus_channel_state_response(1005, 0);
	response = (void *)rsp->data;
	zassert_equal(1, response->publish_count);
	zassert_not_equal(0, response->publish_timestamp);
	zassert_not_equal(0, response->publish_period_avg_ms);
	zassert_equal(sizeof(ambient), rsp->len - sizeof(*response));
	net_buf_unref(rsp);

	zbus_chan_pub(INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_AMBIENT_ENV), &ambient, K_FOREVER);
	zbus_chan_pub(INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_AMBIENT_ENV), &ambient, K_FOREVER);

	send_zbus_channel_state_command(1005, INFUSE_ZBUS_CHAN_AMBIENT_ENV);
	rsp = expect_zbus_channel_state_response(1005, 0);
	response = (void *)rsp->data;
	zassert_equal(3, response->publish_count);
	zassert_not_equal(0, response->publish_timestamp);
	zassert_not_equal(0, response->publish_period_avg_ms);
	zassert_equal(sizeof(ambient), rsp->len - sizeof(*response));
	net_buf_unref(rsp);
}

ZTEST(rpc_command_zbus_channel_state, test_large)
{
	struct rpc_zbus_channel_state_response *response;
	struct large large_data = {0};
	struct net_buf *rsp;

	zbus_chan_pub(&large_channel, &large_data, K_FOREVER);

	send_zbus_channel_state_command(2000, 100);
	rsp = expect_zbus_channel_state_response(2000, 0);
	response = (void *)rsp->data;
	zassert_equal(0, rsp->len - sizeof(*response));
	net_buf_unref(rsp);
}

static void zbus_before(void *fixture)
{
	const struct zbus_channel *chan_bat = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY);
	const struct zbus_channel *chan_env = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_AMBIENT_ENV);

	/* Reset statistics before each test */
	chan_bat->data->publish_timestamp = 0;
	chan_bat->data->publish_count = 0;
	chan_env->data->publish_timestamp = 0;
	chan_env->data->publish_count = 0;
}

ZTEST_SUITE(rpc_command_zbus_channel_state, NULL, NULL, zbus_before, NULL, NULL);
