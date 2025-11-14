/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/audio/dmic.h>

#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/drivers/watchdog.h>
#include <infuse/tdf/definitions.h>
#include <infuse/time/epoch.h>
#include <infuse/zbus/channels.h>

#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/infuse_tasks.h>

static const struct task_schedule schedules[] = {
	{
		.task_id = TASK_ID_TDF_LOGGER,
		.validity = TASK_VALID_ALWAYS,
		.task_args.infuse.tdf_logger =
			{
				.loggers = TDF_DATA_LOGGER_BT_ADV,
				.logging_period_ms = 900,
				.random_delay_ms = 250,
				.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE,
			},
	},
};

TASK_SCHEDULE_STATES_DEFINE(states, schedules);
TASK_RUNNER_TASKS_DEFINE(app_tasks, app_tasks_data, (TDF_LOGGER_TASK, NULL));

/* Empty battery channel, required for TDF broadcast activity */
INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_BATTERY);

/* Microphone configuration */
#define MIC                DT_ALIAS(dmic_dev)
#define MIC_SAMPLE_RATE    16000
#define MIC_BUFFER_SAMPLES 256
K_MEM_SLAB_DEFINE_STATIC(mem_slab, MIC_BUFFER_SAMPLES * sizeof(int16_t), 8, sizeof(void *));
BUILD_ASSERT(PDM_DT_HAS_LEFT_CHANNEL(MIC) + PDM_DT_HAS_RIGHT_CHANNEL(MIC) == 1,
	     "Sample requires a single channel");

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

int main(void)
{
	const struct device *dmic = DEVICE_DT_GET(MIC);
	uint32_t buffer_duration_ms;
	uint32_t buffer_duration_epoch;
	uint16_t tdf_id;
	int rc;

	if (!device_is_ready(dmic)) {
		LOG_ERR("Microphone %s is not ready", dmic->name);
		return -ENODEV;
	}

	/* Start the watchdog */
	(void)infuse_watchdog_start();

	/* Initialise task runner */
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	/* Start auto iteration */
	task_runner_start_auto_iterate();

	LOG_INF("Microphone: %s", dmic->name);

	/* Power up device */
	rc = pm_device_runtime_get(dmic);
	if (rc < 0) {
		LOG_ERR("Failed to power up microphone (%d)", rc);
		return rc;
	}

	struct pcm_stream_cfg stream = {
		.pcm_width = 16,
		.mem_slab = &mem_slab,
	};
	struct dmic_cfg cfg = {
		.io = PDM_DT_IO_CFG_GET(MIC),
		.streams = &stream,
		.channel =
			{
				.req_num_streams = 1,
			},
	};
	cfg.channel.req_num_chan = 1;

#if PDM_DT_HAS_LEFT_CHANNEL(MIC)
	tdf_id = TDF_PCM_16BIT_CHAN_LEFT;
	cfg.channel.req_chan_map_lo = dmic_build_channel_map(0, 0, PDM_CHAN_LEFT);
#elif PDM_DT_HAS_RIGHT_CHANNEL(MIC)
	tdf_id = TDF_PCM_16BIT_CHAN_RIGHT;
	cfg.channel.req_chan_map_lo = dmic_build_channel_map(0, 0, PDM_CHAN_RIGHT);
#endif
	cfg.streams[0].pcm_rate = MIC_SAMPLE_RATE;
	cfg.streams[0].block_size = MIC_BUFFER_SAMPLES * sizeof(int16_t);

	buffer_duration_ms = (1000 * MIC_BUFFER_SAMPLES) / MIC_SAMPLE_RATE;
	buffer_duration_epoch =
		(INFUSE_EPOCH_TIME_TICKS_PER_SEC * MIC_BUFFER_SAMPLES) / MIC_SAMPLE_RATE;
	LOG_INF("Output Rate: %u Hz, Buffer Duration: %u ms", cfg.streams[0].pcm_rate,
		buffer_duration_ms);

	/* Configure microphone */
	rc = dmic_configure(dmic, &cfg);
	if (rc < 0) {
		LOG_ERR("Failed to configure microphone (%d)", rc);
		return rc;
	}

	/* Start the sampling */
	rc = dmic_trigger(dmic, DMIC_TRIGGER_START);
	if (rc < 0) {
		LOG_ERR("Failed to start microphone (%d)", rc);
		return rc;
	}

	struct tdf_idx_array_freq audio_freq = {
		.tdf_id = tdf_id,
		.frequency = MIC_SAMPLE_RATE,
	};
	uint32_t metadata_timestamp = UINT32_MAX;
	uint16_t sample_index = 0;

	for (;;) {

		int16_t *data;
		void *buffer;
		uint32_t size;
		uint32_t num_samples;
		int64_t t_base;

		/* Push audio metadata into the stream every second */
		if (metadata_timestamp != k_uptime_seconds()) {
			TDF_DATA_LOGGER_LOG(TDF_DATA_LOGGER_BT_PERIPHERAL, TDF_IDX_ARRAY_FREQ, 0,
					    &audio_freq);
			metadata_timestamp = k_uptime_seconds();
		}

		/* Read the next buffer from the microphone */
		rc = dmic_read(dmic, 0, &buffer, &size, 2 * buffer_duration_ms);
		if (rc < 0) {
			LOG_ERR("Failed to read from microphone (%d)", rc);
			break;
		}

		/* Rough timestamping */
		t_base = epoch_time_now() - buffer_duration_epoch;
		data = buffer;
		num_samples = size / sizeof(int16_t);

		/* Push data across Bluetooth link */
		while (num_samples) {
			uint8_t to_log = MIN(num_samples, UINT8_MAX);

			tdf_data_logger_log_core(TDF_DATA_LOGGER_BT_PERIPHERAL, tdf_id,
						 sizeof(int16_t), to_log, TDF_DATA_FORMAT_IDX_ARRAY,
						 t_base, sample_index, data);
			t_base = 0;
			data += to_log;
			num_samples -= to_log;
			sample_index += to_log;
		}

		/* Free the memory buffer */
		k_mem_slab_free(&mem_slab, buffer);
	}

	/* Stop the sampling */
	rc = dmic_trigger(dmic, DMIC_TRIGGER_STOP);
	if (rc < 0) {
		LOG_ERR("Failed to stop microphone (%d)", rc);
	}

	(void)pm_device_runtime_put(dmic);
	return 0;
}
