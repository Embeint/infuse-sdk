/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <stdbool.h>

#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/algorithm_runner/runner.h>
#include <infuse/task_runner/runner.h>

static void new_zbus_data(const struct zbus_channel *chan);

ZBUS_LISTENER_DEFINE(runner_listener, new_zbus_data);
ZBUS_GLOBAL_ADD_OBS(runner_listener, 5);

static struct k_work runner;
static sys_slist_t algorithms;

LOG_MODULE_REGISTER(algorithm, CONFIG_ALGORITHM_RUNNER_LOG_LEVEL);

static void new_zbus_data(const struct zbus_channel *chan)
{
	struct algorithm_runner_algorithm *alg, *algs;
	bool run = false;

	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&algorithms, alg, algs, _node) {
		if (alg->config->zbus_channel == chan->id) {
			alg->_changed = chan;
			run = true;
		}
	}

	/* Only queue the executor if the data was relevant for an algorithm */
	if (run) {
		k_work_submit_to_queue(task_runner_work_q(), &runner);
	}
}

static void exec_fn(struct k_work *work)
{
	struct algorithm_runner_algorithm *alg, *algs;

	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&algorithms, alg, algs, _node) {
		/* Only run algorithms that have new data */
		if (alg->_changed == NULL) {
			continue;
		}
		LOG_DBG("Running algorithm %08X on channel %08X", alg->config->algorithm_id,
			alg->_changed->id);
		/* Run algorithm with the channel claimed */
		zbus_chan_claim(alg->_changed, K_FOREVER);
		alg->impl(alg->_changed, alg->config, alg->runtime_state);
		zbus_chan_finish(alg->_changed);
		/* Clear new data flag */
		alg->_changed = NULL;
	}
}

void algorithm_runner_init(void)
{
	sys_slist_init(&algorithms);
	k_work_init(&runner, exec_fn);
}

void algorithm_runner_register(struct algorithm_runner_algorithm *algorithm)
{
	__ASSERT_NO_MSG(algorithm->impl != NULL);

	/* Initialise algorithm */
	algorithm->impl(NULL, algorithm->config, algorithm->runtime_state);

	/* Add to list of algorithms to be run */
	sys_slist_append(&algorithms, &algorithm->_node);
}

void algorithm_runner_tdf_log(const struct algorithm_runner_common_config *config, uint8_t tdf_mask,
			      uint16_t tdf_id, uint8_t tdf_len, uint64_t time, const void *data)
{
	if (config->logging.tdf_mask & tdf_mask) {
		tdf_data_logger_log(config->logging.loggers, tdf_id, tdf_len, time, data);
	}
}
