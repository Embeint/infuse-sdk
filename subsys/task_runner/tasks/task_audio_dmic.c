/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/audio/dmic.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device_runtime.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/tasks/audio_dmic.h>
#include <infuse/tdf/definitions.h>
#include <infuse/tdf/util.h>
#include <infuse/time/epoch.h>

#define BUFFER_SAMPLES CONFIG_TASK_RUNNER_TASK_AUDIO_DMIC_BUFFER_SAMPLE
#define BUFFER_COUNT   CONFIG_TASK_RUNNER_TASK_AUDIO_DMIC_BUFFER_COUNT

K_MEM_SLAB_DEFINE_STATIC(mem_slab, BUFFER_SAMPLES * sizeof(int16_t), BUFFER_COUNT, sizeof(void *));

LOG_MODULE_REGISTER(task_dmic, CONFIG_TASK_AUDIO_DMIC_LOG_LEVEL);

static void dmic_audio_log(const struct task_schedule *schedule, uint16_t tdf_id, uint64_t t_base,
			   uint16_t sample_index, int16_t *data, uint16_t num_data)
{
	if (!task_schedule_tdf_requested(schedule, TASK_AUDIO_DMIC_LOG_SAMPLES)) {
		return;
	}

	while (num_data) {
		uint8_t to_log = MIN(num_data, UINT8_MAX);

		task_schedule_tdf_log_core(schedule, TASK_AUDIO_DMIC_LOG_SAMPLES, tdf_id,
					   sizeof(int16_t), to_log, TDF_DATA_FORMAT_IDX_ARRAY,
					   t_base, sample_index, data);
		t_base = 0;
		data += to_log;
		num_data -= to_log;
		sample_index += to_log;
	}
}

void dmic_task_fn(const struct task_schedule *schedule, struct k_poll_signal *terminate,
		  void *dmic_config)
{
	const struct task_audio_dmic_config *config = dmic_config;
	const struct task_audio_dmic_args *args = &schedule->task_args.infuse.audio_dmic;
	struct pcm_stream_cfg stream = {
		.pcm_width = 16,
		.mem_slab = &mem_slab,
		.gain_db = args->gain_db,
	};
	struct dmic_cfg cfg = {
		.io = config->io_cfg,
		.streams = &stream,
		.channel =
			{
				.req_num_streams = 1,
				.req_num_chan = 1,
			},
	};
	uint32_t buffer_duration_epoch;
	uint32_t buffer_duration_ms;
	uint16_t sample_index = 0;
	uint16_t tdf_id = config->pdm_chan == PDM_CHAN_LEFT ? TDF_PCM_16BIT_CHAN_LEFT
							    : TDF_PCM_16BIT_CHAN_RIGHT;
	int rc;

	cfg.channel.req_chan_map_lo = dmic_build_channel_map(0, 0, config->pdm_chan);
	cfg.streams[0].pcm_rate = args->sample_rate_hz;
	cfg.streams[0].block_size = BUFFER_SAMPLES * sizeof(int16_t);
	buffer_duration_ms = (1000 * BUFFER_SAMPLES) / args->sample_rate_hz;
	buffer_duration_epoch =
		(INFUSE_EPOCH_TIME_TICKS_PER_SEC * BUFFER_SAMPLES) / args->sample_rate_hz;

	/* Request microphone to be powered */
	rc = pm_device_runtime_get(config->dev);
	if (rc < 0) {
		k_sleep(K_SECONDS(1));
		LOG_ERR("Terminating due to %s", "PM failure");
		return;
	}

	/* Configure microphone */
	rc = dmic_configure(config->dev, &cfg);
	if (rc < 0) {
		LOG_ERR("Failed to configure microphone (%d)", rc);
		return;
	}

	LOG_INF("Output Rate: %u Hz, Buffer Duration: %u ms, Gain %d dB", cfg.streams[0].pcm_rate,
		buffer_duration_ms, args->gain_db);

	/* Start the sampling */
	rc = dmic_trigger(config->dev, DMIC_TRIGGER_START);
	if (rc < 0) {
		LOG_ERR("Failed to start microphone (%d)", rc);
		return;
	}

	struct tdf_idx_array_freq audio_freq = {
		.tdf_id = tdf_id,
		.frequency = cfg.streams[0].pcm_rate,
	};

	TASK_SCHEDULE_TDF_LOG(schedule, TASK_AUDIO_DMIC_LOG_METADATA, TDF_IDX_ARRAY_FREQ,
			      epoch_time_now(), &audio_freq);

	/* Read samples until error or requested to stop */
	while (true) {
		int16_t *data;
		void *buffer;
		uint32_t size;
		uint32_t num_samples;
		int64_t t_base;

		/* Read the next buffer from the microphone */
		rc = dmic_read(config->dev, 0, &buffer, &size, 2 * buffer_duration_ms);
		if (rc < 0) {
			LOG_ERR("Failed to read from microphone (%d)", rc);
			break;
		}

		/* Rough timestamping */
		t_base = epoch_time_now() - buffer_duration_epoch;
		data = buffer;
		num_samples = size / sizeof(int16_t);

		/* Log data */
		dmic_audio_log(schedule, tdf_id, t_base, sample_index, data, num_samples);
		sample_index += num_samples;

		/* Free the memory buffer */
		k_mem_slab_free(&mem_slab, buffer);

		/* Check to see if runner has requested termination */
		if (task_runner_task_block(terminate, K_NO_WAIT) == 1) {
			LOG_INF("Terminating due to %s", "runner request");
			break;
		}
	}

	/* Stop the sampling */
	rc = dmic_trigger(config->dev, DMIC_TRIGGER_STOP);
	if (rc < 0) {
		LOG_ERR("Failed to stop microphone (%d)", rc);
	}

	/* Release power requirement */
	rc = pm_device_runtime_put(config->dev);
	if (rc < 0) {
		LOG_ERR("PM put failure");
	}

	/* Terminate thread */
	LOG_DBG("Terminating");
}
