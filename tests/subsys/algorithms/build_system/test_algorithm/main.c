/*
 * Copyright (c) 2025 Embeint Pty Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/llext/symbol.h>

#include <infuse/algorithms/implementation.h>

#include "algorithm_info.h"

static void algorithm_fn(const struct zbus_channel *chan);

const struct algorithm_common_config test_algorithm_config = {
	.algorithm_id = ALGORITHM_ID_EXPECTED,
	.zbus_channel = ALGORITHM_ZBUS_EXPECTED,
	.fn = algorithm_fn,
};
ALGORITHM_EXPORT(test_algorithm_config);

struct algorithm_state {
	uint32_t run_cnt;
};

static void algorithm_fn(const struct zbus_channel *chan)
{
	static struct algorithm_state state;
	const void *data;
	float test_float = 2.75f;
	int test;

	if (chan == NULL) {
		printk("INIT\n");
		return;
	}

	data = zbus_chan_const_msg(chan);
	test = state.run_cnt * test_float;

	printk("RUN: %d %d\n", state.run_cnt, test);
	state.run_cnt += 1;
	zbus_chan_finish(chan);
}
