/*
 * Copyright (c) 2026 Embeint Holdings Pty Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <infuse/algorithms/implementation.h>
#include <infuse/zbus/types.h>

#define TEST_ALGORITHM_ID 0x12344321

static void algorithm_fn(const struct zbus_channel *chan, const struct infuse_algorithm *algorithm,
			 const void *args)
{
	ARG_UNUSED(algorithm);
	ARG_UNUSED(args);

	if (chan != NULL) {
		struct tdf_ambient_temp_pres_hum *environment = (void *)zbus_chan_const_msg(chan);

		environment->temperature += 100;
		zbus_chan_finish(chan);
	}
}

const struct infuse_algorithm test_algorithm = {
	.struct_version = 1,
	.algorithm_id = TEST_ALGORITHM_ID,
	.zbus_channel = INFUSE_ZBUS_CHAN_AMBIENT_ENV,
	.fn = algorithm_fn,
};
ALGORITHM_EXPORT(test_algorithm);
