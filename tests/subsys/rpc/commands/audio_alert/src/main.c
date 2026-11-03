/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <zephyr/drivers/pwm/pwm_fake.h>
#include <zephyr/pm/device_runtime.h>

#include <infuse/rpc/errors.h>
#include <infuse/rpc/types.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>

#define PWM_CLOCK_HZ 16000000

DEFINE_FFF_GLOBALS;

static const struct device *buzzer = DEVICE_DT_GET(DT_ALIAS(buzzer));

static void send_audio_alert_command(uint32_t request_id, struct rpc_audio_alert_request *params)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};

	params->header.request_id = request_id;
	params->header.command_id = RPC_ID_AUDIO_ALERT;

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, params, sizeof(*params));
}

static void expect_audio_alert_response(uint32_t request_id, int16_t rc)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_audio_alert_response *response;
	struct net_buf *rsp;

	zassert_not_null(response_queue);

	/* Response was sent */
	rsp = k_fifo_get(response_queue, K_MSEC(100));
	zassert_not_null(rsp);
	response = (void *)(rsp->data + sizeof(struct epacket_dummy_frame));

	/* Parameters match what we expect */
	zassert_equal(request_id, response->header.request_id);
	zassert_equal(rc, response->header.return_code);

	/* Free the response */
	net_buf_unref(rsp);
}

static void expect_buzzer_usage(int usage)
{
	if (IS_ENABLED(CONFIG_PM_DEVICE_RUNTIME)) {
		zassert_equal(usage, pm_device_runtime_usage(buzzer));
	}
}

static int tones_played(uint32_t *periods, size_t max_periods)
{
	int tones = 0;

	for (int i = 0; i < fake_pwm_set_cycles_fake.call_count; i++) {
		if (fake_pwm_set_cycles_fake.arg3_history[i] == 0) {
			continue;
		}
		if (tones < max_periods) {
			periods[tones] = fake_pwm_set_cycles_fake.arg2_history[i];
		}
		tones++;
	}
	return tones;
}

ZTEST(rpc_command_audio_alert, test_alert)
{
	struct rpc_audio_alert_request params = {
		.frequency_hz = 1000,
		.duration_ms = 1000,
		.burst_length_ms = 100,
		.burst_period_ms = 250,
		.burst_frequency_shift_hz = 500,
		.volume_percent = 100,
	};
	uint32_t expected[] = {
		PWM_CLOCK_HZ / 1000,
		PWM_CLOCK_HZ / 1500,
		PWM_CLOCK_HZ / 2000,
		PWM_CLOCK_HZ / 2500,
	};
	uint32_t periods[8];
	int tones;

	send_audio_alert_command(1, &params);
	expect_audio_alert_response(1, 0);

	/* Buzzer is powered while the alert is running */
	k_sleep(K_MSEC(500));
	expect_buzzer_usage(1);
	k_sleep(K_MSEC(600));
	expect_buzzer_usage(0);

	/* A burst every period, with the frequency shifting each burst */
	tones = tones_played(periods, ARRAY_SIZE(periods));
	zassert_equal(ARRAY_SIZE(expected), tones);
	for (int i = 0; i < tones; i++) {
		zassert_equal(expected[i], periods[i]);
	}
	/* Buzzer is silenced at the end */
	zassert_equal(0, fake_pwm_set_cycles_fake.arg3_val);
}

ZTEST(rpc_command_audio_alert, test_loop)
{
	struct rpc_audio_alert_request params = {
		.frequency_hz = 1000,
		.duration_ms = 1000,
		.burst_length_ms = 100,
		.burst_period_ms = 250,
		.burst_frequency_shift_hz = 500,
		.loop_after = 2,
		.volume_percent = 100,
	};
	uint32_t expected[] = {
		PWM_CLOCK_HZ / 1000,
		PWM_CLOCK_HZ / 1500,
		PWM_CLOCK_HZ / 1000,
		PWM_CLOCK_HZ / 1500,
	};
	uint32_t periods[8];
	int tones;

	send_audio_alert_command(13, &params);
	expect_audio_alert_response(13, 0);
	k_sleep(K_MSEC(1100));
	expect_buzzer_usage(0);

	/* Frequency returns to the base frequency every 2 bursts */
	tones = tones_played(periods, ARRAY_SIZE(periods));
	zassert_equal(ARRAY_SIZE(expected), tones);
	for (int i = 0; i < tones; i++) {
		zassert_equal(expected[i], periods[i]);
	}
}

ZTEST(rpc_command_audio_alert, test_loop_prevents_early_termination)
{
	struct rpc_audio_alert_request params = {
		.frequency_hz = 1000,
		.duration_ms = 1000,
		.burst_length_ms = 50,
		.burst_period_ms = 100,
		.burst_frequency_shift_hz = -400,
		.loop_after = 2,
		.volume_percent = 100,
	};
	uint32_t periods[16];

	send_audio_alert_command(14, &params);
	expect_audio_alert_response(14, 0);

	/* Looping before the frequency goes out of range runs the full duration */
	k_sleep(K_MSEC(500));
	expect_buzzer_usage(1);
	k_sleep(K_MSEC(600));
	expect_buzzer_usage(0);
	zassert_equal(10, tones_played(periods, ARRAY_SIZE(periods)));
	zassert_equal(PWM_CLOCK_HZ / 600, periods[9]);
}

ZTEST(rpc_command_audio_alert, test_negative_shift)
{
	struct rpc_audio_alert_request params = {
		.frequency_hz = 1000,
		.duration_ms = 10000,
		.burst_length_ms = 50,
		.burst_period_ms = 100,
		.burst_frequency_shift_hz = -400,
		.volume_percent = 50,
	};
	uint32_t periods[4];

	send_audio_alert_command(2, &params);
	expect_audio_alert_response(2, 0);

	/* Alert terminates early once the frequency is no longer valid */
	k_sleep(K_MSEC(500));
	expect_buzzer_usage(0);
	zassert_equal(3, tones_played(periods, ARRAY_SIZE(periods)));
	zassert_equal(PWM_CLOCK_HZ / 200, periods[2]);
	/* 50% volume is 25% duty cycle */
	zassert_equal(PWM_CLOCK_HZ / 1000 / 4, fake_pwm_set_cycles_fake.arg3_history[0]);
}

ZTEST(rpc_command_audio_alert, test_frequency_upper_limit)
{
	struct rpc_audio_alert_request params = {
		.frequency_hz = 60000,
		.duration_ms = 10000,
		.burst_length_ms = 5,
		.burst_period_ms = 10,
		.burst_frequency_shift_hz = 3000,
		.volume_percent = 100,
	};
	uint32_t periods[4];

	send_audio_alert_command(9, &params);
	expect_audio_alert_response(9, 0);

	/* Alert terminates early once the frequency exceeds the valid range */
	k_sleep(K_MSEC(100));
	expect_buzzer_usage(0);
	zassert_equal(2, tones_played(periods, ARRAY_SIZE(periods)));
}

ZTEST(rpc_command_audio_alert, test_tone_failure)
{
	struct rpc_audio_alert_request params = {
		.frequency_hz = 1000,
		.duration_ms = 1000,
		.burst_length_ms = 100,
		.burst_period_ms = 250,
		.volume_percent = 100,
	};

	/* Failure to play the first burst is reported in the response */
	fake_pwm_set_cycles_fake.return_val = -EIO;
	send_audio_alert_command(10, &params);
	expect_audio_alert_response(10, INFUSE_RPC_ERROR_AUDIO_TONE_FAILED);
	expect_buzzer_usage(0);

	/* No further bursts are attempted after the failed one */
	fake_pwm_set_cycles_fake.return_val = 0;
	k_sleep(K_MSEC(500));
	zassert_equal(1, tones_played(NULL, 0));
}

ZTEST(rpc_command_audio_alert, test_wait)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_audio_alert_request params = {
		.frequency_hz = 1000,
		.duration_ms = 1000,
		.burst_length_ms = 100,
		.burst_period_ms = 250,
		.volume_percent = 100,
		.wait = 1,
	};
	uint32_t periods[8];

	/* Maximum wait duration is accepted */
	params.duration_ms = 10000;
	send_audio_alert_command(18, &params);
	k_sleep(K_MSEC(10100));
	expect_audio_alert_response(18, 0);
	expect_buzzer_usage(0);
	fake_pwm_set_cycles_reset();

	params.duration_ms = 1000;
	send_audio_alert_command(15, &params);

	/* No response while the alert is running */
	k_sleep(K_MSEC(500));
	expect_buzzer_usage(1);
	zassert_is_null(k_fifo_peek_head(response_queue));

	/* Response once the alert completes */
	k_sleep(K_MSEC(600));
	expect_audio_alert_response(15, 0);
	expect_buzzer_usage(0);
	zassert_equal(4, tones_played(periods, ARRAY_SIZE(periods)));
}

ZTEST(rpc_command_audio_alert, test_wait_tone_failure)
{
	struct rpc_audio_alert_request params = {
		.frequency_hz = 1000,
		.duration_ms = 1000,
		.burst_length_ms = 100,
		.burst_period_ms = 250,
		.volume_percent = 100,
		.wait = 1,
	};
	int return_vals[] = {0, 0, -EIO};

	/* Second burst fails to play (first burst on and off succeed) */
	SET_RETURN_SEQ(fake_pwm_set_cycles, return_vals, ARRAY_SIZE(return_vals));
	send_audio_alert_command(16, &params);

	/* Failure is reported once the alert terminates */
	k_sleep(K_MSEC(400));
	expect_audio_alert_response(16, INFUSE_RPC_ERROR_AUDIO_TONE_FAILED);
	expect_buzzer_usage(0);
	zassert_equal(2, tones_played(NULL, 0));
}

ZTEST(rpc_command_audio_alert, test_cancel)
{
	struct rpc_audio_alert_request params = {
		.frequency_hz = 1000,
		.duration_ms = 10000,
		.burst_length_ms = 100,
		.burst_period_ms = 200,
		.volume_percent = 100,
	};
	uint32_t periods[8];

	send_audio_alert_command(3, &params);
	expect_audio_alert_response(3, 0);
	k_sleep(K_MSEC(300));
	expect_buzzer_usage(1);

	/* Zero duration cancels the running alert */
	params.duration_ms = 0;
	send_audio_alert_command(4, &params);
	expect_audio_alert_response(4, 0);
	expect_buzzer_usage(0);
	zassert_equal(0, fake_pwm_set_cycles_fake.arg3_val);

	/* No further bursts */
	k_sleep(K_MSEC(500));
	zassert_equal(2, tones_played(periods, ARRAY_SIZE(periods)));
}

ZTEST(rpc_command_audio_alert, test_invalid_params)
{
	struct rpc_audio_alert_request valid = {
		.frequency_hz = 1000,
		.duration_ms = 1000,
		.burst_length_ms = 100,
		.burst_period_ms = 250,
		.volume_percent = 100,
	};
	struct rpc_audio_alert_request params;

	params = valid;
	params.frequency_hz = 0;
	send_audio_alert_command(5, &params);
	expect_audio_alert_response(5, INFUSE_RPC_ERROR_INVALID_ARGUMENT);

	params = valid;
	params.burst_length_ms = 0;
	send_audio_alert_command(6, &params);
	expect_audio_alert_response(6, INFUSE_RPC_ERROR_INVALID_ARGUMENT);

	params = valid;
	params.burst_period_ms = 0;
	send_audio_alert_command(7, &params);
	expect_audio_alert_response(7, INFUSE_RPC_ERROR_INVALID_ARGUMENT);

	params = valid;
	params.volume_percent = 101;
	send_audio_alert_command(8, &params);
	expect_audio_alert_response(8, INFUSE_RPC_ERROR_INVALID_ARGUMENT);

	params = valid;
	params.volume_percent = 0;
	send_audio_alert_command(11, &params);
	expect_audio_alert_response(11, INFUSE_RPC_ERROR_INVALID_ARGUMENT);

	params = valid;
	params.burst_length_ms = params.burst_period_ms + 1;
	send_audio_alert_command(12, &params);
	expect_audio_alert_response(12, INFUSE_RPC_ERROR_INVALID_ARGUMENT);

	params = valid;
	params.duration_ms = 10001;
	params.wait = 1;
	send_audio_alert_command(17, &params);
	expect_audio_alert_response(17, INFUSE_RPC_ERROR_INVALID_ARGUMENT);

	/* Nothing was played */
	expect_buzzer_usage(0);
	zassert_equal(0, tones_played(NULL, 0));
}

ZTEST_SUITE(rpc_command_audio_alert, NULL, NULL, NULL, NULL, NULL);
