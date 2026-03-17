/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <infuse/auto/kv_update_log.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/tdf/definitions.h>
#include <infuse/tdf/tdf.h>
#include <infuse/time/epoch.h>

static void kv_update_log_value_changed(uint16_t key, const void *data, size_t data_len,
					void *user_ctx);
static struct kv_store_cb kv_update_log_cb = {
	.value_changed = kv_update_log_value_changed,
};
static uint8_t kv_update_log_mask;

TDF_KVS_VALUE_CHANGED_VAR(tdf_kvs_concrete, CONFIG_INFUSE_AUTO_KV_UPDATE_LOG_MAX_SIZE);

static void kv_update_log_value_changed(uint16_t key, const void *data, size_t data_len,
					void *user_ctx)
{
	size_t to_log = MIN(data_len, CONFIG_INFUSE_AUTO_KV_UPDATE_LOG_MAX_SIZE);
	struct tdf_kvs_concrete tdf;

	tdf.key = key;
	if (kv_store_external_write_only(key) == 0) {
		memcpy(tdf.value, data, to_log);
	} else if (data_len > 0) {
		/* Sensitive data, replace with '*' */
		memset(tdf.value, '*', to_log);
	}

	(void)tdf_data_logger_log(kv_update_log_mask, TDF_KVS_VALUE_CHANGED,
				  sizeof(uint16_t) + data_len, epoch_time_now(), &tdf);
}

void auto_kv_update_log_configure(uint8_t tdf_logger_mask)
{
	kv_update_log_mask = tdf_logger_mask;
	kv_store_register_callback(&kv_update_log_cb);
}
