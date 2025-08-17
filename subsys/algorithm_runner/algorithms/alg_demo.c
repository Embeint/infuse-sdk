/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/algorithm_runner/algorithms/demo.h>
#include <infuse/task_runner/tasks/imu.h>
#include <infuse/time/epoch.h>
#include <infuse/zbus/channels.h>

LOG_MODULE_REGISTER(alg_demo, LOG_LEVEL_INF);

TDF_ALGORITHM_OUTPUT_VAR(tdf_demo_event_output, 1);
TDF_ALGORITHM_OUTPUT_VAR(tdf_demo_state_output, 1);
TDF_ALGORITHM_OUTPUT_VAR(tdf_demo_metric_output, 4);

void algorithm_demo_event_fn(const struct zbus_channel *chan, const void *config, void *data)
{
	const struct algorithm_demo_common_config *c = config;
	struct tdf_demo_event_output tdf;
	uint8_t rand_100;

	if (chan == NULL) {
		/* Nothing to setup */
		return;
	}

	/* Demo algorithm doesn't use the incoming data at all */
	zbus_chan_finish(chan);

	rand_100 = sys_rand32_get() % 100;
	LOG_DBG("Event roll: %d for %d%%", rand_100, c->event_gen_chance);
	if (rand_100 >= c->event_gen_chance) {
		return;
	}
	LOG_INF("Event generated from %d%% chance", c->event_gen_chance);

	/* Populate the event TDF */
	tdf.algorithm_id = c->common.algorithm_id;
	tdf.algorithm_version = 0;
	tdf.output[0] = rand_100;

	/* Log output TDF */
	algorithm_runner_tdf_log(&c->common, ALGORITHM_DEMO_EVENT_LOG, TDF_ALGORITHM_OUTPUT,
				 sizeof(tdf), epoch_time_now(), &tdf);
}

static const uint8_t demo_state_transitions[4][4] = {
	/* State 0 transitions (Rarely to state 2 or 3) */
	{90, 0, 5, 5},
	/* State 1 transitions (75/25 to stay or return to 0) */
	{25, 75, 0, 0},
	/* State 2 transitions (Return to 0 or 1) */
	{10, 15, 75, 0},
	/* State 3 transitions */
	{20, 30, 10, 50},
};

void algorithm_demo_state_fn(const struct zbus_channel *chan, const void *config, void *data)
{
	const struct algorithm_demo_common_config *c = config;
	union algorithm_demo_common_data *d = data;
	struct tdf_demo_state_output tdf;
	const uint8_t *transitions;
	uint8_t cumulative;
	uint8_t rand_100;
	int i;

	if (chan == NULL) {
		d->current_state = 0;
		return;
	}

	/* Demo algorithm doesn't use the incoming data at all */
	zbus_chan_finish(chan);

	transitions = demo_state_transitions[d->current_state];
	rand_100 = sys_rand32_get() % 100;

	/* Determine which state we are transitioning to or staying in */
	cumulative = 0;
	for (i = 0; i < 4; i++) {
		cumulative += transitions[i];
		if (rand_100 < cumulative) {
			break;
		}
	}
	if (i != d->current_state) {
		LOG_INF("Transition from %d to %d", d->current_state, i);
		d->current_state = i;

		/* Populate the event TDF */
		tdf.algorithm_id = c->common.algorithm_id;
		tdf.algorithm_version = 0;
		tdf.output[0] = d->current_state;

		/* Log output TDF */
		algorithm_runner_tdf_log(&c->common, ALGORITHM_DEMO_STATE_LOG, TDF_ALGORITHM_OUTPUT,
					 sizeof(tdf), epoch_time_now(), &tdf);

	} else {
		LOG_DBG("Remain in %d", i);
	}
}

void algorithm_demo_metric_fn(const struct zbus_channel *chan, const void *config, void *data)
{
	const struct algorithm_demo_common_config *c = config;
	union algorithm_demo_common_data *d = data;
	const struct imu_sample_array *samples;
	struct tdf_demo_metric_output tdf;
	k_ticks_t t_event;
	uint32_t metric;

	if (chan == NULL) {
		d->processed = 0;
		return;
	}

	samples = chan->message;

	for (int i = 0; i < samples->accelerometer.num; i++) {
		if (++d->processed < c->compute_metric_len) {
			continue;
		}
		d->processed = 0;

		/* Compute the metric (uptime) */
		metric = k_uptime_seconds();
		LOG_INF("Metric: %d", metric);

		/* Populate the metric TDF */
		tdf.algorithm_id = c->common.algorithm_id;
		tdf.algorithm_version = 0;
		sys_put_le32(metric, tdf.output);

		t_event = imu_sample_timestamp(&samples->accelerometer, i);

		/* Log output TDF */
		algorithm_runner_tdf_log(&c->common, ALGORITHM_DEMO_METRIC_LOG,
					 TDF_ALGORITHM_OUTPUT, sizeof(tdf),
					 epoch_time_from_ticks(t_event), &tdf);
	}

	zbus_chan_finish(chan);
}
