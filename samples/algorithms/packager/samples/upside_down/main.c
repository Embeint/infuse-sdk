/*
 * Copyright (c) 2026 Embeint Holdings Pty Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>

#include <infuse/algorithms/implementation.h>
#include <infuse/zbus/types.h>

#define ALGORITHM_ID 0x00000001

static void algorithm_fn(const struct zbus_channel *chan, const struct infuse_algorithm *algorithm,
			 const void *args)
{
	struct imu_sample_array *imu;
	bool upside_down = false;

	if (chan == NULL) {
		/* No setup required */
		return;
	}

	/* Retrieve the data pointer */
	imu = (void *)zbus_chan_const_msg(chan);
	if (imu->accelerometer.num > 0) {
		/* Last accelerometer sample */
		struct imu_sample *sample = &imu->samples[imu->accelerometer.num - 1];

		upside_down = sample->z < 0;
	}
	zbus_chan_finish(chan);

	printk("Upside down: %d\n", upside_down);
}

const struct infuse_algorithm upside_down_algorithm = {
	.algorithm_id = ALGORITHM_ID,
	.algorithm_version = 1,
	.zbus_channel = INFUSE_ZBUS_CHAN_IMU,
	.fn = algorithm_fn,
};
ALGORITHM_EXPORT(upside_down_algorithm);
