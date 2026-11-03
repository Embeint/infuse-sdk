/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/buzzer.h>
#include <zephyr/pm/device_runtime.h>

#include <infuse/rpc/commands.h>
#include <infuse/rpc/types.h>

LOG_MODULE_DECLARE(rpc_server, CONFIG_INFUSE_RPC_LOG_LEVEL);

#define AUDIO_ALERT_WAIT_MAX_MS 10000

static void audio_alert_burst(struct k_work *work);

static const struct device *buzzer = DEVICE_DT_GET(DT_ALIAS(buzzer));
static K_WORK_DELAYABLE_DEFINE(alert_work, audio_alert_burst);
static K_SEM_DEFINE(alert_done, 0, 1);

static struct audio_alert_state {
	k_timepoint_t end;
	int32_t frequency_hz;
	uint16_t base_frequency_hz;
	int16_t frequency_shift_hz;
	uint16_t loop_after;
	uint16_t loop_count;
	uint16_t burst_length_ms;
	uint16_t burst_period_ms;
	int rc;
	bool active;
} state;

static void audio_alert_finish(int rc)
{
	if (!state.active) {
		return;
	}
	(void)buzzer_stop(buzzer);
	(void)pm_device_runtime_put(buzzer);
	state.rc = rc;
	state.active = false;
	k_sem_give(&alert_done);
}

static int audio_alert_burst_play(void)
{
	uint32_t remaining_ms;
	int rc;

	remaining_ms = k_ticks_to_ms_floor32(sys_timepoint_timeout(state.end).ticks);
	if ((remaining_ms == 0) || (state.frequency_hz <= 0) || (state.frequency_hz > UINT16_MAX)) {
		/* Alert complete, or frequency shifted out of range */
		audio_alert_finish(0);
		return 0;
	}

	/* Play the burst, the buzzer driver handles silencing it */
	rc = buzzer_tone(buzzer, state.frequency_hz, MIN(state.burst_length_ms, remaining_ms));
	if (rc < 0) {
		audio_alert_finish(rc);
		return rc;
	}

	/* Shift the frequency, returning to the base frequency every loop */
	state.frequency_hz += state.frequency_shift_hz;
	if ((state.loop_after > 0) && (++state.loop_count >= state.loop_after)) {
		state.frequency_hz = state.base_frequency_hz;
		state.loop_count = 0;
	}

	/* Schedule the next burst */
	k_work_reschedule(&alert_work, K_MSEC(MIN(state.burst_period_ms, remaining_ms)));
	return 0;
}

static void audio_alert_burst(struct k_work *work)
{
	int rc;

	rc = audio_alert_burst_play();
	if (rc < 0) {
		LOG_ERR("Failed to play tone (%d)", rc);
	}
}

struct net_buf *rpc_command_audio_alert(struct net_buf *request)
{
	struct rpc_audio_alert_request *req = (void *)request->data;
	struct rpc_audio_alert_response rsp = {0};
	struct k_work_sync sync = {0};
	int rc;

	/* Handler can re-arm itself while being cancelled, repeat until idle */
	while (k_work_cancel_delayable_sync(&alert_work, &sync)) {
	}
	audio_alert_finish(0);

	/* Zero duration only cancels the current alert */
	if (req->duration_ms == 0) {
		return rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));
	}

	/* Validate parameters */
	if ((req->frequency_hz == 0) || (req->burst_length_ms == 0) ||
	    (req->burst_period_ms == 0) || (req->burst_length_ms > req->burst_period_ms) ||
	    (req->volume_percent == 0) || (req->volume_percent > BUZZER_VOLUME_MAX) ||
	    (req->wait && (req->duration_ms > AUDIO_ALERT_WAIT_MAX_MS))) {
		return rpc_response_simple_req(request, INFUSE_RPC_ERROR_INVALID_ARGUMENT, &rsp,
					       sizeof(rsp));
	}

	/* Power up the buzzer for the duration of the alert */
	rc = pm_device_runtime_get(buzzer);
	if (rc < 0) {
		return rpc_response_simple_req(request, INFUSE_RPC_ERROR_DEVICE_NOT_READY, &rsp,
					       sizeof(rsp));
	}
	rc = buzzer_set_volume(buzzer, req->volume_percent);
	if (rc < 0) {
		(void)pm_device_runtime_put(buzzer);
		return rpc_response_simple_req(request, INFUSE_RPC_ERROR_AUDIO_VOLUME_SET_FAILED,
					       &rsp, sizeof(rsp));
	}

	state.end = sys_timepoint_calc(K_MSEC(req->duration_ms));
	state.frequency_hz = req->frequency_hz;
	state.base_frequency_hz = req->frequency_hz;
	state.frequency_shift_hz = req->burst_frequency_shift_hz;
	state.loop_after = req->loop_after;
	state.loop_count = 0;
	state.burst_length_ms = req->burst_length_ms;
	state.burst_period_ms = req->burst_period_ms;
	state.rc = 0;
	state.active = true;
	k_sem_reset(&alert_done);

	/* Play the first burst immediately so that errors are reported */
	rc = audio_alert_burst_play();
	if ((rc == 0) && req->wait) {
		/* Block until the alert completes, keeping the RPC server alive */
		while (k_sem_take(&alert_done, K_SECONDS(1)) != 0) {
			rpc_server_watchdog_feed();
		}
		rc = state.rc;
	}
	if (rc < 0) {
		rc = INFUSE_RPC_ERROR_AUDIO_TONE_FAILED;
	}
	return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
}
