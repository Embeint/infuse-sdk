/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/flash/flash_simulator.h>
#include <zephyr/sys/crc.h>

#include <infuse/fs/littlefs.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

#include <lfs.h>

static uint8_t *flash_buffer;
static size_t flash_buffer_size;
static uint8_t test_file_contents[64];

BUILD_ASSERT(IS_ENABLED(CONFIG_INFUSE_LITTLEFS), "LittleFS integration not enabled by default");

ZTEST(littlefs_kv_sync, test_standard_flow)
{
	struct infuse_littlefs_metadata meta = {
		.timestamp = 123,
		.identifier = 0x123,
		.crc = 1000,
	};
	struct kv_littlefs_algorithms_state state;
	struct kv_littlefs_fs_state fs_state;
	uint32_t peak_blocks_used;
	uint32_t init_blocks_used;
	int rc;

	/* Nothing crashes if filesystem not immediately mounted */
	k_sleep(K_MSEC(5000));

	/* Init filesystem */
	rc = infuse_littlefs_init();
	zassert_equal(0, rc);

	/* Algorithm state should be populated, with no stored algorithms */
	k_sleep(K_MSEC(1000));
	rc = KV_STORE_READ(KV_KEY_LITTLEFS_ALGORITHMS_STATE, &state);
	zassert_equal(sizeof(state), rc);
	zassert_equal(0, state.count);
	zassert_equal(0, state.crc_xor);
	rc = KV_STORE_READ(KV_KEY_LITTLEFS_FS_STATE, &fs_state);
	zassert_equal(sizeof(fs_state), rc);
	zassert_equal(LFS_DISK_VERSION, fs_state.disk_version);
	zassert_true(fs_state.block_size > 0);
	zassert_true(fs_state.block_count > 0);
	zassert_true(fs_state.blocks_used > 0);
	init_blocks_used = fs_state.blocks_used;

	/* Create an algorithm, state should update */
	rc = infuse_littlefs_file_create(INFUSE_LFS_FOLDER_ALGORITHMS, 10, &meta);
	zassert_equal(0, rc);
	rc = infuse_littlefs_file_write(test_file_contents, sizeof(test_file_contents));
	zassert_equal(sizeof(test_file_contents), rc);
	rc = infuse_littlefs_file_close();
	zassert_equal(0, rc);
	k_sleep(K_MSEC(1000));
	rc = KV_STORE_READ(KV_KEY_LITTLEFS_ALGORITHMS_STATE, &state);
	zassert_equal(sizeof(state), rc);
	zassert_equal(1, state.count);
	zassert_equal(1000, state.crc_xor);

	/* Create a non-algorithm, no change to algorithm state */
	rc = infuse_littlefs_file_create(INFUSE_LFS_FOLDER_GENERAL, 1, &meta);
	zassert_equal(0, rc);
	for (int i = 0; i < 1024; i += sizeof(test_file_contents)) {
		rc = infuse_littlefs_file_write(test_file_contents, sizeof(test_file_contents));
		zassert_equal(sizeof(test_file_contents), rc);
	}
	rc = infuse_littlefs_file_close();
	zassert_equal(0, rc);
	k_sleep(K_MSEC(1000));
	rc = KV_STORE_READ(KV_KEY_LITTLEFS_ALGORITHMS_STATE, &state);
	zassert_equal(sizeof(state), rc);
	zassert_equal(1, state.count);
	zassert_equal(1000, state.crc_xor);

	/* Create a second algorithm, state should update */
	meta.crc = 0x12345678;
	rc = infuse_littlefs_file_create(INFUSE_LFS_FOLDER_ALGORITHMS, 16, &meta);
	zassert_equal(0, rc);
	rc = infuse_littlefs_file_write(test_file_contents, sizeof(test_file_contents) - 2);
	zassert_equal(sizeof(test_file_contents) - 2, rc);
	rc = infuse_littlefs_file_close();
	zassert_equal(0, rc);
	k_sleep(K_MSEC(1000));
	rc = KV_STORE_READ(KV_KEY_LITTLEFS_ALGORITHMS_STATE, &state);
	zassert_equal(sizeof(state), rc);
	zassert_equal(2, state.count);
	zassert_equal(1000 ^ 0x12345678, state.crc_xor);

	rc = KV_STORE_READ(KV_KEY_LITTLEFS_FS_STATE, &fs_state);
	zassert_equal(sizeof(fs_state), rc);
	peak_blocks_used = fs_state.blocks_used;
	zassert_true(peak_blocks_used > init_blocks_used);

	/* Delete the non-algorithm, no change to state */
	rc = infuse_littlefs_file_delete(INFUSE_LFS_FOLDER_GENERAL, 1);
	zassert_equal(0, rc);
	k_sleep(K_MSEC(1000));
	rc = KV_STORE_READ(KV_KEY_LITTLEFS_ALGORITHMS_STATE, &state);
	zassert_equal(sizeof(state), rc);
	zassert_equal(2, state.count);
	zassert_equal(1000 ^ 0x12345678, state.crc_xor);

	/* Delete the original algorithm  */
	rc = infuse_littlefs_file_delete(INFUSE_LFS_FOLDER_ALGORITHMS, 10);
	zassert_equal(0, rc);
	k_sleep(K_MSEC(1000));
	rc = KV_STORE_READ(KV_KEY_LITTLEFS_ALGORITHMS_STATE, &state);
	zassert_equal(sizeof(state), rc);
	zassert_equal(1, state.count);
	zassert_equal(0x12345678, state.crc_xor);

	/* Delete the second algorithm  */
	rc = infuse_littlefs_file_delete(INFUSE_LFS_FOLDER_ALGORITHMS, 16);
	zassert_equal(0, rc);
	k_sleep(K_MSEC(1000));
	rc = KV_STORE_READ(KV_KEY_LITTLEFS_ALGORITHMS_STATE, &state);
	zassert_equal(sizeof(state), rc);
	zassert_equal(0, state.count);
	zassert_equal(0, state.crc_xor);

	rc = KV_STORE_READ(KV_KEY_LITTLEFS_FS_STATE, &fs_state);
	zassert_equal(sizeof(fs_state), rc);
	zassert_true(fs_state.blocks_used < peak_blocks_used);
}

static bool test_data_init(const void *global_state)
{
	flash_buffer = flash_simulator_get_memory(DEVICE_DT_GET(DT_NODELABEL(sim_flash)),
						  &flash_buffer_size);
	return true;
}

static void test_before(void *fixture)
{
	memset(flash_buffer, 0xFF, flash_buffer_size);
	for (int i = 0; i < sizeof(test_file_contents); i++) {
		test_file_contents[i] = (i + 1) & 0xFF;
	}
}

ZTEST_SUITE(littlefs_kv_sync, test_data_init, NULL, test_before, NULL, NULL);
