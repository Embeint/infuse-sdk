/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdbool.h>

#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/work_q.h>
#include <infuse/algorithm_runner/runner.h>
#include <infuse/task_runner/runner.h>
#include <infuse/fs/kv_types.h>
#include <infuse/fs/kv_store.h>

static void new_zbus_data(const struct zbus_channel *chan);

ZBUS_LISTENER_DEFINE(runner_listener, new_zbus_data);
ZBUS_GLOBAL_ADD_OBS(runner_listener, 5);

static struct k_work runner;
static sys_slist_t algorithms;
static K_SEM_DEFINE(list_lock, 1, 1);

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
		infuse_work_submit(&runner);
	}
}

static void exec_fn(struct k_work *work)
{
	struct algorithm_runner_algorithm *alg, *algs;

	k_sem_take(&list_lock, K_FOREVER);
	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&algorithms, alg, algs, _node) {
		/* Only run algorithms that have new data */
		if (alg->_changed == NULL) {
			continue;
		}
		LOG_DBG("Running algorithm %08X on channel %08X", alg->config->algorithm_id,
			alg->_changed->id);
		/* Run algorithm with the channel claimed */
		zbus_chan_claim(alg->_changed, K_FOREVER);
		alg->impl(alg->_changed, alg->config, alg->arguments, alg->runtime_state);
		/* Clear new data flag */
		alg->_changed = NULL;
	}
	k_sem_give(&list_lock);
}

void algorithm_runner_init(void)
{
	sys_slist_init(&algorithms);
	k_work_init(&runner, exec_fn);
}

void algorithm_runner_register(struct algorithm_runner_algorithm *alg)
{
	__ASSERT_NO_MSG(alg->impl != NULL);

#ifdef CONFIG_KV_STORE
	if (alg->config->arguments_kv_key > 0) {
		if (kv_store_key_data_size(alg->config->arguments_kv_key) ==
		    alg->config->arguments_size) {
			/* Configuration exists in KV store, load it */
			kv_store_read(alg->config->arguments_kv_key, alg->arguments,
				      alg->config->arguments_size);
		} else {
			/* No configuration, or invalid size. Update from defaults */
			kv_store_write(alg->config->arguments_kv_key, alg->arguments,
				       alg->config->arguments_size);
		}
	}
#endif /* CONFIG_KV_STORE */

	/* Initialise alg */
	alg->impl(NULL, alg->config, alg->arguments, alg->runtime_state);

	/* Add to list of algorithms to be run */
	k_sem_take(&list_lock, K_FOREVER);
	sys_slist_append(&algorithms, &alg->_node);
	k_sem_give(&list_lock);
}

bool algorithm_runner_unregister(struct algorithm_runner_algorithm *alg)
{
	bool res;

	/* Remove from list of algorithms to be run */
	k_sem_take(&list_lock, K_FOREVER);
	res = sys_slist_find_and_remove(&algorithms, &alg->_node);
	k_sem_give(&list_lock);

	return res;
}

void algorithm_runner_tdf_log(const struct kv_algorithm_logging *logging, uint8_t tdf_mask,
			      uint16_t tdf_id, uint8_t tdf_len, uint64_t time, const void *data)
{
	if (logging->tdf_mask & tdf_mask) {
		tdf_data_logger_log(logging->loggers, tdf_id, tdf_len, time, data);
	}
}
