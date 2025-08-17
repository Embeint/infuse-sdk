/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>

#include <zephyr/zbus/zbus.h>

#include <infuse/rpc/commands.h>
#include <infuse/drivers/imu.h>
#include <infuse/rpc/types.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/math/statistics.h>
#include <infuse/zbus/channels.h>

INFUSE_ZBUS_CHAN_DECLARE(INFUSE_ZBUS_CHAN_IMU);

LOG_MODULE_DECLARE(rpc_server);

struct net_buf *rpc_command_gravity_reference_update(struct net_buf *request)
{
	KV_KEY_TYPE(KV_KEY_GRAVITY_REFERENCE) gravity;
	const struct zbus_channel *chan = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_IMU);
	struct rpc_gravity_reference_update_request *req = (void *)request->data;
	struct rpc_gravity_reference_update_response rsp = {0};
	struct statistics_state sx = {0}, sy = {0}, sz = {0};
	struct imu_sample_array *imu;
	struct k_sem chan_pub_sem;
	int rc = 0;

	ZBUS_RUNTIME_WAITER_DEFINE(waiter, &chan_pub_sem);

	/* Setup semaphore and add waiter to channel */
	k_sem_init(&chan_pub_sem, 0, 1);
	zbus_chan_add_obs(chan, &waiter, K_FOREVER);

	/* Discard the first buffer */
	rc = k_sem_take(&chan_pub_sem, K_SECONDS(5));
	if (rc < 0) {
		goto unsub;
	}

	/* Wait for the next buffer */
	rc = k_sem_take(&chan_pub_sem, K_SECONDS(5));
	if (rc < 0) {
		goto unsub;
	}

	/* Unsub the waiter */
	zbus_chan_rm_obs(chan, &waiter, K_FOREVER);

	zbus_chan_claim(chan, K_FOREVER);
	imu = chan->message;

	if (imu->accelerometer.num == 0) {
		rc = -ENODATA;
		zbus_chan_finish(chan);
		goto unsub;
	}

	struct imu_sample *s = &imu->samples[imu->accelerometer.offset];

	/* Average all the samples */
	for (int i = 0; i < imu->accelerometer.num; i++) {
		statistics_update(&sx, s->x);
		statistics_update(&sy, s->y);
		statistics_update(&sz, s->z);
		s++;
	}
	zbus_chan_finish(chan);

	rsp.num_samples = imu->accelerometer.num;
	rsp.sample_period_us = k_ticks_to_us_near32(imu->accelerometer.buffer_period_ticks) /
			       imu->accelerometer.num;

	/* Rounding down instead of nearest is fine */
	rsp.reference.x = statistics_mean_rough(&sx);
	rsp.reference.y = statistics_mean_rough(&sy);
	rsp.reference.z = statistics_mean_rough(&sz);
	rsp.variance.x = MIN(statistics_variance_rough(&sx), INT16_MAX);
	rsp.variance.y = MIN(statistics_variance_rough(&sy), INT16_MAX);
	rsp.variance.z = MIN(statistics_variance_rough(&sz), INT16_MAX);

	gravity.x = rsp.reference.x;
	gravity.y = rsp.reference.y;
	gravity.z = rsp.reference.z;
	LOG_INF("Gravity reference: %6d %6d %6d", gravity.x, gravity.y, gravity.z);

	if (req->max_variance == 0 ||
	    ((rsp.variance.x <= req->max_variance) && (rsp.variance.y <= req->max_variance) &&
	     (rsp.variance.z <= req->max_variance))) {
		/* Write updated reference to KV store */
		rc = KV_STORE_WRITE(KV_KEY_GRAVITY_REFERENCE, &gravity);
	} else {
		/* Variance out of bounds */
		LOG_INF("Axis variance > %d", req->max_variance);
		rc = -EIO;
	}

unsub:
	/* Cleanup the waiter */
	zbus_chan_rm_obs(chan, &waiter, K_FOREVER);

	return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
}
