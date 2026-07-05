/*
 * Copyright (c) 2025 Embeint Holdings Pty Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/llext/symbol.h>

#include <infuse/algorithms/implementation.h>
#include <infuse/zbus/types.h>

#include "algorithm_info.h"
#include "helper.h"

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
	const INFUSE_ZBUS_TYPE(INFUSE_ZBUS_CHAN_BATTERY) * data;
	static struct algorithm_state state;
	float test_float = 2.75f;
	int external_test;

	if (chan == NULL) {
		printk("INIT\n");
		return;
	}

	data = zbus_chan_const_msg(chan);
	test_float *= state.run_cnt;
	external_test = get_squared(state.run_cnt);

	printk("RUN: %d RUN*2.75: %.2f RUN^2: %d\n", state.run_cnt, (double)test_float,
	       external_test);
	printk("%d mV %d uA %d%%\n", data->voltage_mv, data->current_ua, data->soc);
	state.run_cnt += 1;
	zbus_chan_finish(chan);
}
