/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/fs/littlefs.h>
#include <infuse/work_q.h>

static struct infuse_littlefs_cb littlefs_cb;
static struct k_work_delayable sync_worker;

LOG_MODULE_REGISTER(littlefs_kv_sync, LOG_LEVEL_INF);

static void file_created(enum infuse_littlefs_folder folder, uint32_t file,
			 struct infuse_littlefs_metadata *meta, void *user_data)
{
	ARG_UNUSED(folder);
	ARG_UNUSED(file);
	ARG_UNUSED(meta);
	ARG_UNUSED(user_data);

	infuse_work_schedule(&sync_worker, K_NO_WAIT);
}

static void file_deleted(enum infuse_littlefs_folder folder, uint32_t file, void *user_data)
{
	ARG_UNUSED(folder);
	ARG_UNUSED(file);
	ARG_UNUSED(user_data);

	infuse_work_schedule(&sync_worker, K_NO_WAIT);
}

static bool alg_folder_cb(enum infuse_littlefs_folder folder, uint32_t file, void *user_data)
{
	struct kv_littlefs_algorithms_state *alg_state = user_data;
	struct infuse_littlefs_metadata meta;

	if (infuse_littlefs_file_metadata(folder, file, &meta) == 0) {
		/* We XOR CRCs so that we are invariant to folder iteration order */
		alg_state->count += 1;
		alg_state->crc_xor ^= meta.crc;
	}
	return true;
}

static void fs_resync(struct k_work *work)
{
	struct kv_littlefs_algorithms_state alg_state = {0};
	int rc;

	LOG_DBG("Computing filesystem algorithm state");
	rc = infuse_littlefs_folder_iter(INFUSE_LFS_FOLDER_ALGORITHMS, alg_folder_cb, &alg_state);
	if ((rc < 0) && (rc != -ENOENT)) {
		/* Failed to iterate folder, most likely because of lock, try again after delay */
		if (rc == -EINVAL) {
			/* Don't spam the logs if it was the lock */
			LOG_WRN("Failed to iterate algorithm folder (%d), rescheduling", rc);
		} else if (rc == -EAGAIN) {
			LOG_WRN("Filesystem not mounted");
		}
		infuse_work_schedule(&sync_worker, K_SECONDS(1));
		return;
	}

	rc = kv_store_write(KV_KEY_LITTLEFS_ALGORITHMS_STATE, &alg_state, sizeof(alg_state));
	if (rc == 0) {
		LOG_DBG("No change to algorithm state");
	} else if (rc == sizeof(alg_state)) {
		LOG_INF("Algorithm state: Count %d, XOR %08x", alg_state.count, alg_state.crc_xor);
	} else {
		LOG_ERR("Failed to write algorithm state (%d)", rc);
	}

	struct kv_littlefs_fs_state fs_state = {0};
	struct infuse_littlefs_fs_info info;

	rc = infuse_littlefs_fs_info(&info);
	if (rc == 0) {
		fs_state.disk_version = info.disk_version;
		fs_state.block_size = info.block_size;
		fs_state.block_count = info.block_count;
		fs_state.blocks_used = info.blocks_used;

		rc = kv_store_write(KV_KEY_LITTLEFS_FS_STATE, &fs_state, sizeof(fs_state));
		if (rc == 0) {
			LOG_DBG("No change to filesystem state");
		} else if (rc == sizeof(fs_state)) {
			LOG_INF("Filesystem state: %d/%d blocks (%d bytes) used", info.blocks_used,
				info.block_count, info.block_size);
		} else {
			LOG_ERR("Failed to write filesystem state (%d)", rc);
		}
	}
}

static int littlefs_kv_sync_init(void)
{
	k_work_init_delayable(&sync_worker, fs_resync);
	littlefs_cb.file_created = file_created;
	littlefs_cb.file_deleted = file_deleted;

	/* Register for callbacks */
	infuse_littlefs_register_cb(&littlefs_cb);

	/* Initial computation on boot */
	infuse_work_schedule(&sync_worker, K_NO_WAIT);
	return 0;
}

SYS_INIT(littlefs_kv_sync_init, APPLICATION, 90);
