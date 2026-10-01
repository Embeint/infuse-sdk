/*
 * Copyright (c) 2026 Embeint Holdings Pty Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>

#include <infuse/algorithms/implementation.h>
#include <infuse/zbus/types.h>

#define TEST_ALGORITHM_ID 0x12349876

static bool initialised;

static void algorithm_fn(const struct zbus_channel *chan, const struct infuse_algorithm *algorithm,
			 const void *args)
{
	if (chan == NULL) {
		initialised = true;
		return;
	}

	struct tdf_ambient_temp_pres_hum *environment = (void *)zbus_chan_const_msg(chan);

	/* The host expects +1, proving initialisation ran before the channel publish. */
	environment->temperature += initialised ? 1 : 100;
	zbus_chan_finish(chan);
}

const struct infuse_algorithm test_algorithm = {
	.algorithm_id = TEST_ALGORITHM_ID,
	.algorithm_version = 2,
	.zbus_channel = INFUSE_ZBUS_CHAN_AMBIENT_ENV,
	.fn = algorithm_fn,
};
ALGORITHM_EXPORT(test_algorithm);
