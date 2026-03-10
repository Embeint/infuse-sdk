/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/audio/dmic.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/util.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#include <infuse/math/statistics.h>

#include <infuse/validation/core.h>
#include <infuse/validation/dmic.h>

#define TEST "DMIC"

/* Microphone configuration */
#define MIC                DT_ALIAS(dmic_dev)
#define MIC_SAMPLE_RATE    16000
#define MIC_BUFFER_SAMPLES 256
K_MEM_SLAB_DEFINE_STATIC(mem_slab, MIC_BUFFER_SAMPLES * sizeof(int16_t), 8, sizeof(void *));
BUILD_ASSERT(PDM_DT_HAS_LEFT_CHANNEL(MIC) + PDM_DT_HAS_RIGHT_CHANNEL(MIC) == 1,
	     "Sample requires a single channel");

static int validation_dmic_sample(const struct device *dev)
{
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
				.req_num_chan = 1,
			},
	};
	struct statistics_state audio_stats;
	uint32_t buffer_duration_ms;
	k_timepoint_t expiry;
	int buffer_count = 0;
	int rc;

#if PDM_DT_HAS_LEFT_CHANNEL(MIC)
	cfg.channel.req_chan_map_lo = dmic_build_channel_map(0, 0, PDM_CHAN_LEFT);
#elif PDM_DT_HAS_RIGHT_CHANNEL(MIC)
	cfg.channel.req_chan_map_lo = dmic_build_channel_map(0, 0, PDM_CHAN_RIGHT);
#endif
	cfg.streams[0].pcm_rate = MIC_SAMPLE_RATE;
	cfg.streams[0].block_size = MIC_BUFFER_SAMPLES * sizeof(int16_t);
	buffer_duration_ms = (1000 * MIC_BUFFER_SAMPLES) / MIC_SAMPLE_RATE;

	/* Configure microphone */
	rc = dmic_configure(dev, &cfg);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to configure microphone (%d)", rc);
		return rc;
	}

	/* Start the sampling */
	rc = dmic_trigger(dev, DMIC_TRIGGER_START);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to start microphone (%d)", rc);
		return rc;
	}

	VALIDATION_REPORT_INFO(TEST, "Output Rate: %u Hz (Buffer %d ms)", cfg.streams[0].pcm_rate,
			       buffer_duration_ms);
	statistics_reset(&audio_stats);

	expiry = sys_timepoint_calc(K_SECONDS(CONFIG_INFUSE_VALIDATION_DMIC_ANALYZE_DURATION));
	while (!sys_timepoint_expired(expiry)) {
		int16_t *data;
		void *buffer;
		uint32_t size;
		uint32_t num_samples;

		/* Read the next buffer from the microphone */
		rc = dmic_read(dev, 0, &buffer, &size, 2 * buffer_duration_ms);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "Failed to read from microphone (%d)", rc);
			return rc;
		}

		if (buffer_count++ <= CONFIG_INFUSE_VALIDATION_DMIC_BUFFERS_DROP) {
			/* Drop the first buffer due to startup transients */
			k_mem_slab_free(&mem_slab, buffer);
			continue;
		}

		data = buffer;
		num_samples = size / sizeof(int16_t);

		/* Feed samples into statistics */
		for (int i = 0; i < num_samples; i++) {
			statistics_update(&audio_stats, data[i]);
		}

		/* Free the memory buffer */
		k_mem_slab_free(&mem_slab, buffer);
	}

	/* Stop the sampling */
	rc = dmic_trigger(dev, DMIC_TRIGGER_STOP);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to stop microphone (%d)", rc);
	}

	if (rc == 0) {
		/* Output signal statistics */
		VALIDATION_REPORT_VALUE(TEST, "SAMPLES", "%d", audio_stats.n);
		VALIDATION_REPORT_VALUE(TEST, "MEAN", "%d", (int)statistics_mean(&audio_stats));
		VALIDATION_REPORT_VALUE(TEST, "VARIANCE", "%d",
					(int)statistics_variance(&audio_stats));
	}
	return rc;
}

int infuse_validation_dmic(const struct device *dev, uint8_t flags)
{
	int rc;

	VALIDATION_REPORT_INFO(TEST, "DEV=%s", dev->name);

	/* Validation currently hardcoded to work with `dmic_dev` alias */
	if (dev != DEVICE_DT_GET(MIC)) {
		VALIDATION_REPORT_ERROR(TEST, "Unexpected microphone device");
		rc = -ENODEV;
		goto test_end;
	}

	/* Power up device */
	rc = pm_device_runtime_get(dev);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "pm_device_runtime_get (%d)", rc);
		goto test_end;
	}

	if (flags & VALIDATION_DMIC_STATISTICAL_SAMPLE) {
		rc = validation_dmic_sample(dev);
	}

	/* Power down device */
	if (pm_device_runtime_put(dev) < 0) {
		if (rc == 0) {
			VALIDATION_REPORT_ERROR(TEST, "pm_device_runtime_put");
			rc = -EIO;
		}
	}
test_end:
	if (rc == 0) {
		VALIDATION_REPORT_PASS(TEST, "DEV=%s", dev->name);
	}

	return rc;
}
