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
#include "shared.h"

static void algorithm_fn(const struct zbus_channel *chan, const struct infuse_algorithm *algorithm,
			 const void *args);

const struct infuse_algorithm gesture_algorithm = {
	.algorithm_id = 0x12349877,
	.zbus_channel = INFUSE_ZBUS_CHAN_IMU,
	.fn = algorithm_fn,
};
ALGORITHM_EXPORT(gesture_algorithm);

const char *class_strings[] = {
	"Double Shake", "Double Tap", "Triple Tap", "CW rotate", "CCW rotate", "Idle", "Unknown",
};

#define USER_UNIQ_INPUTS_NUM 4
#define BUFFERED_OUTPUTS     8

TDF_ALGORITHM_CLASS_TIME_SERIES_VAR(predicted_classes_type, BUFFERED_OUTPUTS);
struct predicted_classes_type predicted_classes;

static void algorithm_fn(const struct zbus_channel *chan, const struct infuse_algorithm *algorithm,
			 const void *args)
{
	const INFUSE_ZBUS_TYPE(INFUSE_ZBUS_CHAN_IMU) * imu_data;
	nrf_edgeai_t *user_model = nrf_edgeai_user_model();
	flt32_t input_sample[USER_UNIQ_INPUTS_NUM];
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
		predicted_classes.algorithm_version =
			0; // to-do update when algorithm_version is added

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
		input_sample[0] = sqrtf((float)acc_sample->x * (float)acc_sample->x +
					(float)acc_sample->y * (float)acc_sample->y +
					(float)acc_sample->z * (float)acc_sample->z);
		input_sample[1] = (float)acc_sample->x;
		input_sample[2] = (float)acc_sample->y;
		input_sample[3] = (float)acc_sample->z;
		/* Feed this sample pair into the model's windowing buffer */
		res = nrf_edgeai_feed_inputs(user_model, input_sample, USER_UNIQ_INPUTS_NUM);
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
		       (double)user_model->decoded_output.classif.probabilities
			       .p_f32[predicted_class]);

		predicted_classes.values[inference_idx++] = predicted_class;
		if (inference_idx >= BUFFERED_OUTPUTS) {
			broadcast_inference(sizeof(struct tdf_algorithm_class_time_series) +
						    BUFFERED_OUTPUTS,
					    inference_window_start, &predicted_classes);
			inference_idx = 0;
			inference_window_start = inference_window_start +
						 i * imu_data->accelerometer.buffer_period_ticks /
							 imu_data->accelerometer.num;
		}
	}
	if (inference_idx) {
		predicted_classes.algorithm_id = gesture_algorithm.algorithm_id;
		predicted_classes.algorithm_version =
			0; // to-do update when algorithm_version is added
		broadcast_inference(sizeof(struct tdf_algorithm_class_time_series) + inference_idx,
				    inference_window_start, &predicted_classes);
		inference_idx = 0;
	}

	zbus_chan_finish(chan);
}
