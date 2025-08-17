/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/rpc/commands.h>
#include <infuse/rpc/types.h>
#include <infuse/time/epoch.h>

LOG_MODULE_DECLARE(rpc_server);

struct net_buf *rpc_command_zbus_channel_state(struct net_buf *request)
{
	struct rpc_zbus_channel_state_request *req = (void *)request->data;
	struct rpc_zbus_channel_state_response rsp = {0};
	const struct zbus_channel *chan = zbus_chan_from_id(req->channel_id);
	struct net_buf *response;

	if (chan == NULL) {
		/* Bad channel ID */
		return rpc_response_simple_req(request, -EBADF, &rsp, sizeof(rsp));
	}
	if (zbus_chan_pub_stats_count(chan) == 0) {
		/* No data published yet */
		return rpc_response_simple_req(request, -EAGAIN, &rsp, sizeof(rsp));
	}

#ifdef CONFIG_ZBUS_CHANNEL_PUBLISH_STATS
	/* Channel statistics */
	rsp.publish_timestamp = epoch_time_from_ticks(zbus_chan_pub_stats_last_time(chan));
	rsp.publish_count = zbus_chan_pub_stats_count(chan);
	rsp.publish_period_avg_ms = zbus_chan_pub_stats_avg_period(chan);
#endif /* CONFIG_ZBUS_CHANNEL_PUBLISH_STATS */

	response = rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));

	/* Populate channel data if it fits */
	if (chan->message_size <= net_buf_tailroom(response)) {
		zbus_chan_read(chan, net_buf_tail(response), K_FOREVER);
		net_buf_add(response, chan->message_size);
	}
	return response;
}
