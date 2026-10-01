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
#ifdef CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER
#include <zephyr/llext/llext.h>

#include <infuse/fs/littlefs.h>

#include "llext_littlefs_loader.h"
#endif /* CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER */
#include <infuse/reboot.h>

static void new_zbus_data(const struct zbus_channel *chan);

ZBUS_LISTENER_DEFINE(runner_listener, new_zbus_data);
ZBUS_GLOBAL_ADD_OBS(runner_listener, 5);

static struct k_work runner;
static sys_slist_t algorithms;
/* Writers take list_lock before listener_lock. Never block while holding listener_lock. */
static K_MUTEX_DEFINE(list_lock);
static struct k_spinlock listener_lock;

struct algorithm_runner_algorithm {
	const struct infuse_algorithm *algorithm;
	const struct zbus_channel *changed;
#ifdef CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER
	struct llext *extension;
#endif /* CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER */
	bool allocated;
	bool reload;
	bool superseded;
	sys_snode_t node;
};

static struct algorithm_runner_algorithm algorithm_pool[CONFIG_ALGORITHM_RUNNER_MAX_ALGORITHMS];

#ifdef CONFIG_KV_STORE
static struct kv_store_cb alg_kv_cb;
#endif /* CONFIG_KV_STORE */
#ifdef CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER
static struct infuse_littlefs_cb alg_littlefs_cb;
static struct k_work_delayable algorithm_file_retry_work;

#define ALGORITHM_FILE_RETRY_DELAY K_MSEC(100)

enum algorithm_file_action {
	ALGORITHM_FILE_LOAD,
	ALGORITHM_FILE_REMOVE,
};

struct algorithm_file_event {
	uint32_t file;
	enum algorithm_file_action action;
};

K_MSGQ_DEFINE(algorithm_file_events, sizeof(struct algorithm_file_event),
	      2 * CONFIG_ALGORITHM_RUNNER_MAX_ALGORITHMS, __alignof__(struct algorithm_file_event));
#endif /* CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER */

LOG_MODULE_REGISTER(algorithm, CONFIG_ALGORITHM_RUNNER_LOG_LEVEL);

static bool algorithm_supersedes(struct algorithm_runner_algorithm *a,
				 struct algorithm_runner_algorithm *b)
{
	return (a->algorithm->algorithm_id == b->algorithm->algorithm_id) &&
	       (a->algorithm->algorithm_version > b->algorithm->algorithm_version);
}

/* Caller must hold list_lock and listener_lock. */
static void algorithm_superseded_update(void)
{
	struct algorithm_runner_algorithm *algorithm, *candidate;

	SYS_SLIST_FOR_EACH_CONTAINER(&algorithms, algorithm, node) {
		algorithm->superseded = false;
		SYS_SLIST_FOR_EACH_CONTAINER(&algorithms, candidate, node) {
			if (algorithm_supersedes(candidate, algorithm)) {
				algorithm->superseded = true;
				algorithm->changed = NULL;
				break;
			}
		}
	}
}

static void new_zbus_data(const struct zbus_channel *chan)
{
	struct algorithm_runner_algorithm *alg;
	k_spinlock_key_t key;
	bool run = false;

	key = k_spin_lock(&listener_lock);
	SYS_SLIST_FOR_EACH_CONTAINER(&algorithms, alg, node) {
		if ((alg->algorithm->zbus_channel == chan->id) && !alg->superseded) {
			alg->changed = chan;
			run = true;
		}
	}
	k_spin_unlock(&listener_lock, key);

	/* Only queue the executor if the data was relevant for an algorithm */
	if (run) {
		infuse_work_submit(&runner);
	}
}

#ifdef CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER

static int algorithm_file_remove(uint32_t file)
{
	struct algorithm_runner_algorithm *alg;
	struct llext *extension = NULL;
	k_spinlock_key_t key;
	int rc = -ENOENT;

	k_mutex_lock(&list_lock, K_FOREVER);
	key = k_spin_lock(&listener_lock);
	SYS_SLIST_FOR_EACH_CONTAINER(&algorithms, alg, node) {
		if ((alg->extension != NULL) && (alg->algorithm->algorithm_id == file)) {
			sys_slist_find_and_remove(&algorithms, &alg->node);
			alg->allocated = false;
			extension = alg->extension;
			alg->extension = NULL;
			rc = 0;
			break;
		}
	}
	if (rc == 0) {
		algorithm_superseded_update();
	}
	k_spin_unlock(&listener_lock, key);
	k_mutex_unlock(&list_lock);

	if (extension != NULL) {
		rc = llext_unload(&extension);
	}
	return rc;
}

static int algorithm_file_load(uint32_t file)
{
	const struct infuse_algorithm *algorithm;
	struct algorithm_runner_algorithm *alg;
	struct llext *extension = NULL;
	int rc;

	rc = llext_littlefs_load(file, &extension, &algorithm);
	if (rc < 0) {
		return rc;
	}

	rc = algorithm_runner_register(algorithm);
	if (rc < 0) {
		goto unload;
	}

	k_mutex_lock(&list_lock, K_FOREVER);
	SYS_SLIST_FOR_EACH_CONTAINER(&algorithms, alg, node) {
		if (alg->algorithm == algorithm) {
			alg->extension = extension;
			break;
		}
	}
	k_mutex_unlock(&list_lock);
	return 0;

unload:
	(void)llext_unload(&extension);
	return rc;
}

static void algorithm_file_events_process(void)
{
	struct algorithm_file_event event;
	int rc;

	/* Peek so a busy load remains queued along with every event behind it. */
	while (k_msgq_peek(&algorithm_file_events, &event) == 0) {
		if (event.action == ALGORITHM_FILE_LOAD) {
			/* Replace an existing filesystem algorithm with the same ID. */
			(void)algorithm_file_remove(event.file);
			rc = algorithm_file_load(event.file);
			if (rc == -EBUSY) {
				LOG_DBG("Filesystem busy loading algorithm file %08X, retrying",
					event.file);
				infuse_work_reschedule(&algorithm_file_retry_work,
						       ALGORITHM_FILE_RETRY_DELAY);
				return;
			}
			if (rc < 0) {
				LOG_ERR("Failed to load algorithm file %08X (%d)", event.file, rc);
			} else {
				LOG_INF("Loaded algorithm file %08X", event.file);
			}
		} else {
			rc = algorithm_file_remove(event.file);
			if (rc < 0) {
				LOG_WRN("Failed to remove algorithm file %08X (%d)", event.file,
					rc);
			} else {
				LOG_INF("Removed algorithm file %08X", event.file);
			}
		}

		/* Remove the event only after it has reached a terminal result. */
		(void)k_msgq_get(&algorithm_file_events, &event, K_NO_WAIT);
	}
}

static void algorithm_file_retry_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	infuse_work_submit(&runner);
}

static void algorithm_file_event_submit(uint32_t file, enum algorithm_file_action action)
{
	const struct algorithm_file_event event = {
		.file = file,
		.action = action,
	};

	if (k_msgq_put(&algorithm_file_events, &event, K_NO_WAIT) < 0) {
		LOG_ERR("Filesystem algorithm event queue full");
		return;
	}
	infuse_work_submit(&runner);
}

#endif /* CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER */

#ifdef CONFIG_KV_STORE

static bool handle_alg_reload(struct algorithm_runner_algorithm *alg)
{
	k_spinlock_key_t key;
	int read_len;

	/* Configuration changed in KV store */
	read_len = kv_store_read(alg->algorithm->arguments_kv_key, alg->algorithm->arguments,
				 alg->algorithm->arguments_size);
	if (read_len != alg->algorithm->arguments_size) {
#ifdef CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER
		struct llext *extension = alg->extension;
#endif /* CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER */
#ifdef CONFIG_INFUSE_REBOOT
		/* Invalid written configuration, but we no-longer have the
		 * default values from the static variable. Force a reboot, which
		 * will reset the configuration.
		 */
		infuse_reboot_delayed(INFUSE_REBOOT_CFG_CHANGE, alg->algorithm->algorithm_id,
				      alg->algorithm->arguments_kv_key, K_SECONDS(2));
#endif /* CONFIG_INFUSE_REBOOT */
		/* Reboot failed or is not enabled, unregister the algorithm,
		 * nothing else we can do.
		 */
		LOG_WRN("Invalid configuration for %08X, unregistering",
			alg->algorithm->algorithm_id);
		key = k_spin_lock(&listener_lock);
		sys_slist_find_and_remove(&algorithms, &alg->node);
		alg->allocated = false;
#ifdef CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER
		alg->extension = NULL;
#endif /* CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER */
		algorithm_superseded_update();
		k_spin_unlock(&listener_lock, key);
#ifdef CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER
		if (extension != NULL) {
			(void)llext_unload(&extension);
		}
#endif /* CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER */
		return false;
	}
	/* Re-initialise the algorithm */
	LOG_DBG("Re-initialising algorithm %08X", alg->algorithm->algorithm_id);
	alg->algorithm->fn(NULL, alg->algorithm, alg->algorithm->arguments);
	/* Don't reload again */
	alg->reload = false;

	return true;
}

#endif /* CONFIG_KV_STORE */

static void exec_fn(struct k_work *work)
{
	struct algorithm_runner_algorithm *alg, *algs;
	const struct zbus_channel *changed;
	k_spinlock_key_t key;

#ifdef CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER
	algorithm_file_events_process();
#endif /* CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER */

	k_mutex_lock(&list_lock, K_FOREVER);
	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&algorithms, alg, algs, node) {
#ifdef CONFIG_KV_STORE

		if (alg->reload) {
			if (!handle_alg_reload(alg)) {
				continue;
			}
		}
#endif /* CONFIG_KV_STORE */
		/* Snapshot and clear the pending channel without holding the spinlock while the
		 * algorithm runs. A publication during execution remains pending for the next run.
		 */
		key = k_spin_lock(&listener_lock);
		if (alg->superseded) {
			alg->changed = NULL;
			changed = NULL;
		} else {
			changed = alg->changed;
			alg->changed = NULL;
		}
		k_spin_unlock(&listener_lock, key);

		/* Only run algorithms that have new data */
		if (changed == NULL) {
			continue;
		}
		LOG_DBG("Running algorithm %08X on channel %08X", alg->algorithm->algorithm_id,
			changed->id);
		/* Run algorithm with the channel claimed */
		zbus_chan_claim(changed, K_FOREVER);
		alg->algorithm->fn(changed, alg->algorithm, alg->algorithm->arguments);
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
		if (key == alg->algorithm->arguments_kv_key) {
			/* Arguments have changed, force a reload before next run */
			alg->reload = true;
		}
	}
	k_mutex_unlock(&list_lock);
}

#endif /* CONFIG_KV_STORE */

#ifdef CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER

static void alg_file_created(enum infuse_littlefs_folder folder, uint32_t file,
			     struct infuse_littlefs_metadata *meta, void *user_data)
{
	ARG_UNUSED(meta);
	ARG_UNUSED(user_data);

	if (folder == INFUSE_LFS_FOLDER_ALGORITHMS) {
		LOG_DBG("Algorithm file %08X created", file);
		algorithm_file_event_submit(file, ALGORITHM_FILE_LOAD);
	}
}

static void alg_file_deleted(enum infuse_littlefs_folder folder, uint32_t file, void *user_data)
{
	ARG_UNUSED(user_data);

	if (folder == INFUSE_LFS_FOLDER_ALGORITHMS) {
		LOG_DBG("Algorithm file %08X deleted", file);
		algorithm_file_event_submit(file, ALGORITHM_FILE_REMOVE);
	}
}

static bool alg_file_existing(enum infuse_littlefs_folder folder, uint32_t file, void *user_data)
{
	ARG_UNUSED(folder);
	ARG_UNUSED(user_data);

	algorithm_file_event_submit(file, ALGORITHM_FILE_LOAD);
	return true;
}

#endif /* CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER */

void algorithm_runner_init(void)
{
	k_spinlock_key_t key;

	k_mutex_lock(&list_lock, K_FOREVER);
	key = k_spin_lock(&listener_lock);
	sys_slist_init(&algorithms);
	memset(algorithm_pool, 0, sizeof(algorithm_pool));
	k_spin_unlock(&listener_lock, key);
	k_mutex_unlock(&list_lock);
	k_work_init(&runner, exec_fn);
#ifdef CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER
	k_work_init_delayable(&algorithm_file_retry_work, algorithm_file_retry_fn);
#endif /* CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER */

#ifdef CONFIG_KV_STORE
	alg_kv_cb.value_changed = alg_kv_value_changed;
	kv_store_register_callback(&alg_kv_cb);
#endif /* CONFIG_KV_STORE */
#ifdef CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER
	int rc;

	alg_littlefs_cb.file_created = alg_file_created;
	alg_littlefs_cb.file_deleted = alg_file_deleted;
	infuse_littlefs_register_cb(&alg_littlefs_cb);

	rc = infuse_littlefs_folder_iter(INFUSE_LFS_FOLDER_ALGORITHMS, alg_file_existing, NULL);
	if ((rc < 0) && (rc != -ENOENT)) {
		LOG_WRN("Failed to find existing algorithm files (%d)", rc);
	}
#endif /* CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER */
}

#ifdef CONFIG_ZTEST
void algorithm_runner_reset(void)
{
	struct k_work_sync sync;
	k_spinlock_key_t key;
#ifdef CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER
	struct k_work_sync retry_sync;

	k_work_cancel_delayable_sync(&algorithm_file_retry_work, &retry_sync);
#endif /* CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER */

	/* Ensure the runner cannot access the algorithm list while it is reset. */
	k_work_cancel_sync(&runner, &sync);

#ifdef CONFIG_KV_STORE
	kv_store_unregister_callback(&alg_kv_cb);
#endif /* CONFIG_KV_STORE */
#ifdef CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER
	infuse_littlefs_unregister_cb(&alg_littlefs_cb);
	k_msgq_purge(&algorithm_file_events);
#endif /* CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER */

	k_mutex_lock(&list_lock, K_FOREVER);
	key = k_spin_lock(&listener_lock);
	sys_slist_init(&algorithms);
	k_spin_unlock(&listener_lock, key);
#ifdef CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER
	for (size_t i = 0; i < ARRAY_SIZE(algorithm_pool); i++) {
		if (algorithm_pool[i].extension != NULL) {
			(void)llext_unload(&algorithm_pool[i].extension);
		}
	}
#endif /* CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER */
	memset(algorithm_pool, 0, sizeof(algorithm_pool));
	k_mutex_unlock(&list_lock);
}
#endif /* CONFIG_ZTEST */

int algorithm_runner_register(const struct infuse_algorithm *algorithm)
{
	struct algorithm_runner_algorithm *alg = NULL;
	k_spinlock_key_t key;
	__maybe_unused int rc = 0;

	if ((algorithm == NULL) || (algorithm->fn == NULL) ||
	    ((algorithm->arguments_size > 0) && (algorithm->arguments == NULL))) {
		return -EINVAL;
	}

	k_mutex_lock(&list_lock, K_FOREVER);
	for (size_t i = 0; i < ARRAY_SIZE(algorithm_pool); i++) {
		if (algorithm_pool[i].allocated && (algorithm_pool[i].algorithm == algorithm)) {
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
	alg->algorithm = algorithm;
	alg->allocated = true;
	k_mutex_unlock(&list_lock);

#ifdef CONFIG_KV_STORE
	if (algorithm->arguments_kv_key > 0) {
		rc = kv_store_key_data_size(algorithm->arguments_kv_key);
		if (rc == algorithm->arguments_size) {
			/* Configuration exists in KV store, load it */
			rc = kv_store_read(algorithm->arguments_kv_key, algorithm->arguments,
					   algorithm->arguments_size);
		} else if ((rc < 0) && (rc != -ENOENT)) {
			goto free_algorithm;
		} else {
			/* No configuration, or invalid size. Update from defaults */
			rc = kv_store_write(algorithm->arguments_kv_key, algorithm->arguments,
					    algorithm->arguments_size);
		}
		if (rc < 0) {
			goto free_algorithm;
		}
		if (rc != algorithm->arguments_size) {
			rc = -EIO;
			goto free_algorithm;
		}
	}
#endif /* CONFIG_KV_STORE */

	/* Initialise alg */
	algorithm->fn(NULL, algorithm, algorithm->arguments);

	/* Add to list of algorithms to be run */
	k_mutex_lock(&list_lock, K_FOREVER);
	key = k_spin_lock(&listener_lock);
	sys_slist_append(&algorithms, &alg->node);
	algorithm_superseded_update();
	k_spin_unlock(&listener_lock, key);
	k_mutex_unlock(&list_lock);

	return 0;

#ifdef CONFIG_KV_STORE
free_algorithm:
#endif /* CONFIG_KV_STORE */
	k_mutex_lock(&list_lock, K_FOREVER);
	alg->allocated = false;
	k_mutex_unlock(&list_lock);
	return rc;
}

int algorithm_runner_unregister(const struct infuse_algorithm *algorithm)
{
	struct algorithm_runner_algorithm *alg;
	k_spinlock_key_t key;
	int rc = -ENOENT;

	if (algorithm == NULL) {
		return -EINVAL;
	}

	/* Remove from list of algorithms to be run */
	k_mutex_lock(&list_lock, K_FOREVER);
	key = k_spin_lock(&listener_lock);
	SYS_SLIST_FOR_EACH_CONTAINER(&algorithms, alg, node) {
		if (alg->algorithm == algorithm) {
			sys_slist_find_and_remove(&algorithms, &alg->node);
			alg->allocated = false;
			algorithm_superseded_update();
			rc = 0;
			break;
		}
	}
	k_spin_unlock(&listener_lock, key);
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
