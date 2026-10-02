/*
 * Copyright (c) 2025 Embeint Holdings Pty Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/llext/symbol.h>

#include <nrf_edgeai/nrf_edgeai.h>

#include <infuse/algorithms/implementation.h>
#include <infuse/algorithms/nrf_edge_ai_helpers.h>
#include <infuse/zbus/types.h>

#include "nrf_edgeai_generated/nrf_edgeai_user_model.h"
#include "nrf_edgeai_generated/nrf_edgeai_user_types.h"

#include "shared.h"
#include "model_macros.h"

static void algorithm_fn(const struct zbus_channel *chan, const struct infuse_algorithm *algorithm,
			 const void *args);

const struct infuse_algorithm gesture_algorithm = {
	.algorithm_id = 0x12349877,
	.algorithm_version = 0,
	.zbus_channel = INFUSE_ZBUS_CHAN_IMU,
	.fn = algorithm_fn,
};
ALGORITHM_EXPORT(gesture_algorithm);

const char *class_strings[] = {
	"Double Shake", "Double Tap", "Triple Tap", "CW rotate", "CCW rotate", "Idle", "Unknown",
};

#define NUM_BUFFERED_OUTPUTS 8

TDF_ALGORITHM_CLASS_TIME_SERIES_VAR(predicted_classes_type, NUM_BUFFERED_OUTPUTS);
struct predicted_classes_type predicted_classes;

static float magnitude(float x, float y, float z)
{
	return sqrtf(x * x + y * y + z * z);
}

static void algorithm_fn(const struct zbus_channel *chan, const struct infuse_algorithm *algorithm,
			 const void *args)
{
	const INFUSE_ZBUS_TYPE(INFUSE_ZBUS_CHAN_IMU) * imu_data;
	nrf_edgeai_t *user_model = nrf_edgeai_user_model();
	nrf_user_input_t input_sample[MODEL_OUTPUTS_NUM];
	const struct imu_sample *acc_sample;
	nrf_edgeai_err_t res;
	uint16_t acc_start;
	uint8_t inference_idx = 0;
	uint64_t inference_window_start;
	uint16_t predicted_class;

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
		predicted_classes.algorithm_id = gesture_algorithm.algorithm_id;
		predicted_classes.algorithm_version = gesture_algorithm.algorithm_version;

		printk("Initialized model\n");
		return;
	}

	/* Feed data into the model.*/
	imu_data = zbus_chan_const_msg(chan);
	acc_start = imu_data->accelerometer.offset;
	inference_window_start = imu_data->accelerometer.timestamp_ticks;
	for (int i = 0; i < imu_data->accelerometer.num; i++) {
		/* This model expects Floating Point inputs, and a magnitude */
		acc_sample = &imu_data->samples[acc_start + i];
		input_sample[0] = magnitude(acc_sample->x, acc_sample->y, acc_sample->z);
		input_sample[1] = acc_sample->x;
		input_sample[2] = acc_sample->y;
		input_sample[3] = acc_sample->z;

		/* Feed this sample pair into the model's windowing buffer */
		res = nrf_edgeai_feed_inputs(user_model, input_sample,
					     INPUT_UNIQ_FEATURES_USED_NUM);
		if (res == NRF_EDGEAI_ERR_INPROGRESS) {
			continue;
		}

		/* Input buffer has reached samples required - run inference on the window */
		inference_led_pulse(true);
		res = nrf_edgeai_run_inference(user_model);
		inference_led_pulse(false);
		if (res != NRF_EDGEAI_ERR_SUCCESS) {
			printk("Failed to run inference (%d)\n", res);
			continue;
		}

		predicted_class = user_model->decoded_output.classif.predicted_class;
		printk("Inference completed: ");
		printk("class: %s (%d) %.3f\n", class_strings[predicted_class], predicted_class,
		       get_probability_as_double(&user_model->decoded_output.classif,
						 predicted_class));

		predicted_classes.values[inference_idx++] = predicted_class;
		if (inference_idx >= NUM_BUFFERED_OUTPUTS) {
			broadcast_inference(sizeof(struct tdf_algorithm_class_time_series) +
						    NUM_BUFFERED_OUTPUTS,
					    inference_window_start, &predicted_classes);
			inference_idx = 0;
			inference_window_start = inference_window_start +
						 i * imu_data->accelerometer.buffer_period_ticks /
							 imu_data->accelerometer.num;
		}
	}
	if (inference_idx) {
		broadcast_inference(sizeof(struct tdf_algorithm_class_time_series) + inference_idx,
				    inference_window_start, &predicted_classes);
		inference_idx = 0;
	}

	zbus_chan_finish(chan);
}
