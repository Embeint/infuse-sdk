/*
 * Copyright (c) 2025 Embeint Holdings Pty Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/llext/symbol.h>

#include <nrf_edgeai/nrf_edgeai.h>

#include <infuse/algorithms/implementation.h>
#include <infuse/zbus/types.h>

#include "nrf_edgeai_generated/nrf_edgeai_user_model.h"
#include "algorithm_info.h"

static void algorithm_fn(const struct zbus_channel *chan);

const struct algorithm_common_config test_algorithm_config = {
	.algorithm_id = ALGORITHM_ID_EXPECTED,
	.zbus_channel = ALGORITHM_ZBUS_EXPECTED,
	.fn = algorithm_fn,
};
ALGORITHM_EXPORT(test_algorithm_config);

#define USER_UNIQ_INPUTS_NUM 2

static void algorithm_fn(const struct zbus_channel *chan)
{
	const INFUSE_ZBUS_TYPE(INFUSE_ZBUS_CHAN_BATTERY) * data;
	nrf_edgeai_t *user_model = nrf_edgeai_user_model();
	flt32_t input_sample[2];
	nrf_edgeai_err_t res;

	if (chan == NULL) {
		/* Validate model compatibility */
		if (!nrf_edgeai_is_runtime_compatible(user_model)) {
			printk("Model incompatible with runtime\n");
			return;
		}
		/* Initialise the model */
		res = nrf_edgeai_init(user_model);
		if (res != NRF_EDGEAI_ERR_SUCCESS) {
			printk("Failed to initialise model (%d)\n", res);
		}
		printk("Initialized model\n");
		return;
	}

	/* Feed data into the model.
	 * The data is not compatible with the model, but we're only checking
	 * the general flow, not validating outputs.
	 */
	data = zbus_chan_const_msg(chan);
	input_sample[0] = data->voltage_mv / 1000.0f;
	input_sample[1] = data->current_ua / 1000000.0f;
	zbus_chan_finish(chan);

	/* Feed this sample pair into the model's windowing buffer */
	res = nrf_edgeai_feed_inputs(user_model, input_sample, USER_UNIQ_INPUTS_NUM);
	if (res == NRF_EDGEAI_ERR_INPROGRESS) {
		return;
	}

	/* Input buffer has reached samples required - run inference on the window */
	res = nrf_edgeai_run_inference(user_model);
	if (res != NRF_EDGEAI_ERR_SUCCESS) {
		printk("Failed to run inference (%d)\n", res);
		return;
	}
	printk("Inference anomaly score: %.6f\n", (double)user_model->decoded_output.anomaly.score);
}
