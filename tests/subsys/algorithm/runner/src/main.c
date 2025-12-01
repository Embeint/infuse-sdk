/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <infuse/algorithm_runner/runner.h>
#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/drivers/imu/data_types.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/math/statistics.h>
#include <infuse/states.h>
#include <infuse/reboot.h>
#include <infuse/task_runner/runner.h>
#include <infuse/zbus/channels.h>

struct algorithm_args {
	uint32_t arg;
} __packed;

struct algorithm_state {
	const struct zbus_channel *expected_chan;
	const struct zbus_channel *next_chan;
	uint32_t expected_arg;
	uint32_t run_cnt;
};

const struct algorithm_runner_common_config alg1_config = {
	.algorithm_id = 0x12345678,
	.zbus_channel = INFUSE_ZBUS_CHAN_BATTERY,
	.arguments_size = sizeof(struct algorithm_args),
	.state_size = sizeof(struct algorithm_state),
	/* Use the TILT arguments key for testing */
	.arguments_kv_key = KV_KEY_ALG_TILT_ARGS,
};
const struct algorithm_runner_common_config alg2_config = {
	.algorithm_id = 0xAAAA0000,
	.zbus_channel = INFUSE_ZBUS_CHAN_BATTERY,
	.state_size = sizeof(struct algorithm_state),
};
const struct algorithm_runner_common_config alg3_config = {
	.algorithm_id = 00001234,
	.zbus_channel = INFUSE_ZBUS_CHAN_AMBIENT_ENV,
	.state_size = sizeof(struct algorithm_state),
};
struct algorithm_state alg1_state = {0};
struct algorithm_state alg2_state = {0};
struct algorithm_state alg3_state = {0};

INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_BATTERY);
INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_AMBIENT_ENV);
INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_LOCATION);
static K_SEM_DEFINE(reboot_request, 0, 1);

void infuse_reboot(enum infuse_reboot_reason reason, uint32_t info1, uint32_t info2)
{
	k_sem_give(&reboot_request);
}

void infuse_reboot_delayed(enum infuse_reboot_reason reason, uint32_t info1, uint32_t info2,
			   k_timeout_t delay)
{
	k_sem_give(&reboot_request);
}

static void algorithm_impl(const struct zbus_channel *chan,
			   const struct algorithm_runner_common_config *common, const void *args,
			   void *data)
{
	const struct algorithm_args *a = args;
	struct algorithm_state *d = data;

	zassert_not_null(common);
	zassert_not_null(data);
	zassert_equal(d->expected_chan, chan);
	if (chan) {
		zbus_chan_finish(chan);
	}
	if (d->expected_arg) {
		zassert_not_null(a);
		zassert_equal(a->arg, d->expected_arg);
	} else {
		zassert_is_null(a);
	}
	if (d->next_chan) {
		/* Update for the next run */
		d->expected_chan = d->next_chan;
		d->next_chan = NULL;
	}

	d->run_cnt += 1;
}

ZTEST(algorithm_runner, test_running)
{
	struct algorithm_args args1 = {
		.arg = 0x1234,
	};
	struct algorithm_runner_algorithm alg1 = {
		.impl = algorithm_impl,
		.config = &alg1_config,
		.arguments = &args1,
		.runtime_state = &alg1_state,
	};
	struct algorithm_runner_algorithm alg2 = {
		.impl = algorithm_impl,
		.config = &alg2_config,
		.runtime_state = &alg2_state,
	};
	struct algorithm_runner_algorithm alg3 = {
		.impl = algorithm_impl,
		.config = &alg3_config,
		.runtime_state = &alg3_state,
	};
	struct tdf_battery_state battery = {0};
	struct tdf_ambient_temp_pres_hum ambient_env = {0};
	struct tdf_gcs_wgs84_llha location = {0};

	algorithm_runner_init();

	zassert_equal(-ENOENT, kv_store_key_data_size(KV_KEY_ALG_TILT_ARGS));
	alg1_state.expected_arg = args1.arg;

	zassert_false(algorithm_runner_unregister(&alg1));
	zassert_false(algorithm_runner_unregister(&alg2));
	zassert_false(algorithm_runner_unregister(&alg3));
	algorithm_runner_register(&alg1);
	algorithm_runner_register(&alg2);
	algorithm_runner_register(&alg3);

	/* Arguments written to specified key on registration */
	zassert_equal(sizeof(args1), kv_store_key_data_size(KV_KEY_ALG_TILT_ARGS));

	/* Each should have been run once on registration with "chan == NULL" */
	zassert_equal(1, alg1_state.run_cnt);
	zassert_equal(1, alg2_state.run_cnt);
	zassert_equal(1, alg3_state.run_cnt);

	/* Channel should be supplied to all future calls */
	alg1_state.expected_chan = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY);
	alg2_state.expected_chan = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY);
	alg3_state.expected_chan = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_AMBIENT_ENV);

	/* Publishing to battery should iterate alg1 and alg2 */
	zbus_chan_pub(INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY), &battery, K_FOREVER);
	k_sleep(K_MSEC(10));

	zassert_equal(2, alg1_state.run_cnt);
	zassert_equal(2, alg2_state.run_cnt);
	zassert_equal(1, alg3_state.run_cnt);

	/* Publishing to environmental should iterate alg3 */
	zbus_chan_pub(INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_AMBIENT_ENV), &ambient_env, K_FOREVER);
	k_sleep(K_MSEC(10));

	zassert_equal(2, alg1_state.run_cnt);
	zassert_equal(2, alg2_state.run_cnt);
	zassert_equal(2, alg3_state.run_cnt);

	/* Publishing to location should do nothing */
	zbus_chan_pub(INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_LOCATION), &location, K_FOREVER);
	k_sleep(K_MSEC(10));

	zassert_equal(2, alg1_state.run_cnt);
	zassert_equal(2, alg2_state.run_cnt);
	zassert_equal(2, alg3_state.run_cnt);

	/* Publish to battery many times */
	for (int i = 3; i < 10; i++) {
		zbus_chan_pub(INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY), &battery, K_FOREVER);
		k_sleep(K_MSEC(10));

		zassert_equal(i, alg1_state.run_cnt);
		zassert_equal(i, alg2_state.run_cnt);
		zassert_equal(2, alg3_state.run_cnt);
	}

	/* Unregister alg2, battery should no longer result in alg2 running  */
	zassert_true(algorithm_runner_unregister(&alg2));
	zbus_chan_pub(INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY), &battery, K_FOREVER);
	k_sleep(K_MSEC(10));

	zassert_equal(10, alg1_state.run_cnt);
	zassert_equal(9, alg2_state.run_cnt);

	/* Unregister remaining algorithms, no more iteration */
	zassert_true(algorithm_runner_unregister(&alg1));
	zassert_true(algorithm_runner_unregister(&alg3));

	zbus_chan_pub(INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY), &battery, K_FOREVER);
	zbus_chan_pub(INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_AMBIENT_ENV), &ambient_env, K_FOREVER);
	k_sleep(K_MSEC(10));

	zassert_equal(10, alg1_state.run_cnt);
	zassert_equal(9, alg2_state.run_cnt);
	zassert_equal(2, alg3_state.run_cnt);

	zassert_false(algorithm_runner_unregister(&alg1));
	zassert_false(algorithm_runner_unregister(&alg2));
	zassert_false(algorithm_runner_unregister(&alg3));

	/* Incorrect length should be overwritten on registration */
	uint8_t bad_value = -1;

	zassert_equal(sizeof(bad_value),
		      kv_store_write(KV_KEY_ALG_TILT_ARGS, &bad_value, sizeof(bad_value)));

	alg1_state.expected_chan = NULL;
	algorithm_runner_register(&alg1);
	zassert_equal(sizeof(args1), kv_store_key_data_size(KV_KEY_ALG_TILT_ARGS));
	zassert_true(algorithm_runner_unregister(&alg1));

	/* KV value should be used */
	struct algorithm_args args_updated = {
		.arg = 0xFFAA,
	};

	zassert_equal(sizeof(args_updated),
		      kv_store_write(KV_KEY_ALG_TILT_ARGS, &args_updated, sizeof(args_updated)));
	alg1_state.expected_arg = args_updated.arg;
	algorithm_runner_register(&alg1);
	zassert_equal(sizeof(args1), kv_store_key_data_size(KV_KEY_ALG_TILT_ARGS));

	/* Reset run counts */
	alg1_state.run_cnt = 0;
	alg1_state.expected_chan = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY);

	/* Run twice */
	zbus_chan_pub(INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY), &battery, K_FOREVER);
	zbus_chan_pub(INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY), &battery, K_FOREVER);
	zassert_equal(2, alg1_state.run_cnt);

	/* Write new configuration values, reconfigure should be called */
	args_updated.arg = 0x1000AABB;
	alg1_state.expected_arg = args_updated.arg;
	alg1_state.expected_chan = NULL;
	alg1_state.next_chan = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY);

	zassert_equal(sizeof(args_updated),
		      kv_store_write(KV_KEY_ALG_TILT_ARGS, &args_updated, sizeof(args_updated)));

	zbus_chan_pub(INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY), &battery, K_FOREVER);
	zassert_equal(4, alg1_state.run_cnt);

	/* Write a bad configuration, should trigger reboot (With no algorithm run) */
	zassert_equal(sizeof(bad_value),
		      kv_store_write(KV_KEY_ALG_TILT_ARGS, &bad_value, sizeof(bad_value)));

	zassert_equal(-EBUSY, k_sem_take(&reboot_request, K_NO_WAIT));
	zbus_chan_pub(INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY), &battery, K_FOREVER);
	zassert_equal(4, alg1_state.run_cnt);
	zassert_equal(0, k_sem_take(&reboot_request, K_MSEC(100)));

	/* Algorithm should have been automatically unregistered */
	zassert_false(algorithm_runner_unregister(&alg1));
}

ZTEST(algorithm_runner, test_logging)
{
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();

	const struct kv_algorithm_logging logging = {
		.loggers = TDF_DATA_LOGGER_SERIAL,
		.tdf_mask = BIT(1),
	};
	struct tdf_acc_4g data;
	struct net_buf *tx;

	zassert_not_null(tx_fifo);

	/* Not requested */
	algorithm_runner_tdf_log(&logging, BIT(0), TDF_ACC_4G, sizeof(data), 0, &data);
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	tx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_is_null(tx);

	/* Requested */
	algorithm_runner_tdf_log(&logging, BIT(1), TDF_ACC_4G, sizeof(data), 0, &data);
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	tx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_not_null(tx);
	net_buf_unref(tx);

	/* Not requested */
	algorithm_runner_tdf_log(&logging, BIT(2), TDF_ACC_4G, sizeof(data), 0, &data);
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	tx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_is_null(tx);
}

ZTEST_SUITE(algorithm_runner, NULL, NULL, NULL, NULL, NULL);
