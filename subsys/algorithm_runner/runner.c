/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/slist.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/work_q.h>
#include <infuse/algorithm_runner/runner.h>
#include <infuse/task_runner/runner.h>
#include <infuse/fs/kv_types.h>
#include <infuse/fs/kv_store.h>
#include <infuse/reboot.h>

static void new_zbus_data(const struct zbus_channel *chan);

ZBUS_LISTENER_DEFINE(runner_listener, new_zbus_data);
ZBUS_GLOBAL_ADD_OBS(runner_listener, 5);

static struct k_work runner;
static sys_slist_t algorithms;
static K_MUTEX_DEFINE(list_lock);

struct algorithm_runner_algorithm {
	const struct algorithm_common_config *config;
	const struct zbus_channel *changed;
	bool allocated;
	bool reload;
	sys_snode_t node;
};

static struct algorithm_runner_algorithm algorithm_pool[CONFIG_ALGORITHM_RUNNER_MAX_ALGORITHMS];

LOG_MODULE_REGISTER(algorithm, CONFIG_ALGORITHM_RUNNER_LOG_LEVEL);

static void new_zbus_data(const struct zbus_channel *chan)
{
	struct algorithm_runner_algorithm *alg;
	bool run = false;

	SYS_SLIST_FOR_EACH_CONTAINER(&algorithms, alg, node) {
		if (alg->config->zbus_channel == chan->id) {
			alg->changed = chan;
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

	k_mutex_lock(&list_lock, K_FOREVER);
	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&algorithms, alg, algs, node) {
#ifdef CONFIG_KV_STORE
		int read_len;

		if (alg->reload) {
			/* Configuration changed in KV store */
			read_len =
				kv_store_read(alg->config->arguments_kv_key, alg->config->arguments,
					      alg->config->arguments_size);
			if (read_len != alg->config->arguments_size) {
#ifdef CONFIG_INFUSE_REBOOT
				/* Invalid written configuration, but we no-longer have the
				 * default values from the static variable. Force a reboot, which
				 * will reset the configuration.
				 */
				infuse_reboot_delayed(INFUSE_REBOOT_CFG_CHANGE,
						      alg->config->algorithm_id,
						      alg->config->arguments_kv_key, K_SECONDS(2));
#endif /* CONFIG_INFUSE_REBOOT */
				/* Reboot failed or is not enabled, unregister the algorithm,
				 * nothing else we can do.
				 */
				LOG_WRN("Invalid configuration for %08X, unregistering",
					alg->config->algorithm_id);
				sys_slist_find_and_remove(&algorithms, &alg->node);
				alg->allocated = false;
				continue;
			}
			/* Re-initialise the algorithm */
			LOG_DBG("Re-initialising algorithm %08X", alg->config->algorithm_id);
			alg->config->fn(NULL, alg->config, alg->config->arguments);
			/* Don't reload again */
			alg->reload = false;
		}
#endif /* CONFIG_KV_STORE */
		/* Only run algorithms that have new data */
		if (alg->changed == NULL) {
			continue;
		}
		LOG_DBG("Running algorithm %08X on channel %08X", alg->config->algorithm_id,
			alg->changed->id);
		/* Run algorithm with the channel claimed */
		zbus_chan_claim(alg->changed, K_FOREVER);
		alg->config->fn(alg->changed, alg->config, alg->config->arguments);
		/* Clear new data flag */
		alg->changed = NULL;
	}
	k_mutex_unlock(&list_lock);
}

#ifdef CONFIG_KV_STORE

static void alg_kv_value_changed(uint16_t key, const void *data, size_t data_len, void *user_ctx)
{
	struct algorithm_runner_algorithm *alg;

	/* Iterate over linked algorithms */
	k_mutex_lock(&list_lock, K_FOREVER);
	SYS_SLIST_FOR_EACH_CONTAINER(&algorithms, alg, node) {
		if (key == alg->config->arguments_kv_key) {
			/* Arguments have changed, force a reload before next run */
			alg->reload = true;
		}
	}
	k_mutex_unlock(&list_lock);
}

#endif /* CONFIG_KV_STORE */

void algorithm_runner_init(void)
{
#ifdef CONFIG_KV_STORE
	static struct kv_store_cb alg_kv_cb = {
		.value_changed = alg_kv_value_changed,
	};

	/* Check is only to handle tests that call `algorithm_runner_init` multiple times */
	if (runner.handler == NULL) {
		kv_store_register_callback(&alg_kv_cb);
	}
#endif /* CONFIG_KV_STORE */

	k_mutex_lock(&list_lock, K_FOREVER);
	sys_slist_init(&algorithms);
	memset(algorithm_pool, 0, sizeof(algorithm_pool));
	k_mutex_unlock(&list_lock);
	k_work_init(&runner, exec_fn);
}

int algorithm_runner_register(const struct algorithm_common_config *config)
{
	struct algorithm_runner_algorithm *alg = NULL;
	int rc = 0;

	if ((config == NULL) || (config->fn == NULL) ||
	    ((config->arguments_size > 0) && (config->arguments == NULL))) {
		return -EINVAL;
	}

	k_mutex_lock(&list_lock, K_FOREVER);
	for (size_t i = 0; i < ARRAY_SIZE(algorithm_pool); i++) {
		if (algorithm_pool[i].allocated && (algorithm_pool[i].config == config)) {
			k_mutex_unlock(&list_lock);
			return -EALREADY;
		}
		if ((alg == NULL) && !algorithm_pool[i].allocated) {
			alg = &algorithm_pool[i];
		}
	}
	if (alg == NULL) {
		k_mutex_unlock(&list_lock);
		return -ENOMEM;
	}
	memset(alg, 0, sizeof(*alg));
	alg->config = config;
	alg->allocated = true;
	k_mutex_unlock(&list_lock);

#ifdef CONFIG_KV_STORE
	if (config->arguments_kv_key > 0) {
		rc = kv_store_key_data_size(config->arguments_kv_key);
		if (rc == config->arguments_size) {
			/* Configuration exists in KV store, load it */
			rc = kv_store_read(config->arguments_kv_key, config->arguments,
					   config->arguments_size);
		} else if ((rc < 0) && (rc != -ENOENT)) {
			goto free_algorithm;
		} else {
			/* No configuration, or invalid size. Update from defaults */
			rc = kv_store_write(config->arguments_kv_key, config->arguments,
					    config->arguments_size);
		}
		if (rc < 0) {
			goto free_algorithm;
		}
		if (rc != config->arguments_size) {
			rc = -EIO;
			goto free_algorithm;
		}
	}
#endif /* CONFIG_KV_STORE */

	/* Initialise alg */
	config->fn(NULL, config, config->arguments);

	/* Add to list of algorithms to be run */
	k_mutex_lock(&list_lock, K_FOREVER);
	sys_slist_append(&algorithms, &alg->node);
	k_mutex_unlock(&list_lock);

	return 0;

free_algorithm:
	k_mutex_lock(&list_lock, K_FOREVER);
	alg->allocated = false;
	k_mutex_unlock(&list_lock);
	return rc;
}

int algorithm_runner_unregister(const struct algorithm_common_config *config)
{
	struct algorithm_runner_algorithm *alg;
	int rc = -ENOENT;

	if (config == NULL) {
		return -EINVAL;
	}

	/* Remove from list of algorithms to be run */
	k_mutex_lock(&list_lock, K_FOREVER);
	SYS_SLIST_FOR_EACH_CONTAINER(&algorithms, alg, node) {
		if (alg->config == config) {
			sys_slist_find_and_remove(&algorithms, &alg->node);
			alg->allocated = false;
			rc = 0;
			break;
		}
	}
	k_mutex_unlock(&list_lock);

	return rc;
}

void algorithm_runner_tdf_log(const struct kv_algorithm_logging *logging, uint8_t tdf_mask,
			      uint16_t tdf_id, uint8_t tdf_len, uint64_t time, const void *data)
{
	if (logging->tdf_mask & tdf_mask) {
		tdf_data_logger_log(logging->loggers, tdf_id, tdf_len, time, data);
	}
}
