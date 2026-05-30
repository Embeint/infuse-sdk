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

#include <lfs.h>

static uint8_t *flash_buffer;
static size_t flash_buffer_size;
static uint8_t test_file_contents[64];

BUILD_ASSERT(IS_ENABLED(CONFIG_INFUSE_LITTLEFS), "LittleFS integration not enabled by default");

ZTEST(infuse_littlefs, test_init_double)
{
	int rc;

	/* First mount works */
	rc = infuse_littlefs_init();
	zassert_equal(0, rc);

	/* Second mount works as well */
	rc = infuse_littlefs_init();
	zassert_equal(0, rc);
}

ZTEST(infuse_littlefs, test_create_api_usage_error)
{
	struct infuse_littlefs_metadata meta = {0};
	struct infuse_littlefs_fs_info fs_state;
	uint8_t buffer[8] = {0};
	uint32_t crc;
	int rc;

	/* Functions before mount */
	rc = infuse_littlefs_fs_info(&fs_state);
	zassert_equal(-EAGAIN, rc);
	rc = infuse_littlefs_file_create(INFUSE_LFS_FOLDER_A_GNSS, 0, &meta);
	zassert_equal(-EAGAIN, rc);
	rc = infuse_littlefs_file_open(INFUSE_LFS_FOLDER_A_GNSS, 0);
	zassert_equal(-EAGAIN, rc);
	rc = infuse_littlefs_file_size(INFUSE_LFS_FOLDER_A_GNSS, 0);
	zassert_equal(-EAGAIN, rc);
	rc = infuse_littlefs_file_metadata(INFUSE_LFS_FOLDER_A_GNSS, 0, &meta);
	zassert_equal(-EAGAIN, rc);
	rc = infuse_littlefs_file_crc32(INFUSE_LFS_FOLDER_A_GNSS, 0, UINT32_MAX, &crc, buffer,
					sizeof(buffer));
	zassert_equal(-EAGAIN, rc);

	/* Init filesystem */
	rc = infuse_littlefs_init();
	zassert_equal(0, rc);

	/* Close before open */
	rc = infuse_littlefs_file_close();
	zassert_equal(-EINVAL, rc);

	/* Operations with write file open */
	rc = infuse_littlefs_file_create(INFUSE_LFS_FOLDER_GENERAL, 3, &meta);
	zassert_equal(0, rc);
	rc = infuse_littlefs_file_create(INFUSE_LFS_FOLDER_GENERAL, 3, &meta);
	zassert_equal(-EINVAL, rc);
	rc = infuse_littlefs_fs_info(&fs_state);
	zassert_equal(-EINVAL, rc);
	rc = infuse_littlefs_file_open(INFUSE_LFS_FOLDER_A_GNSS, 3);
	zassert_equal(-EINVAL, rc);
	rc = infuse_littlefs_file_read(buffer, sizeof(buffer));
	zassert_equal(-EINVAL, rc);
	rc = infuse_littlefs_file_seek(0);
	zassert_equal(-EINVAL, rc);
	rc = infuse_littlefs_file_delete(INFUSE_LFS_FOLDER_A_GNSS, 3);
	zassert_equal(-EINVAL, rc);
	rc = infuse_littlefs_file_close();
	zassert_equal(0, rc);

	/* Operations with read file open */
	rc = infuse_littlefs_file_open(INFUSE_LFS_FOLDER_GENERAL, 3);
	zassert_equal(0, rc);
	rc = infuse_littlefs_file_create(INFUSE_LFS_FOLDER_GENERAL, 3, &meta);
	zassert_equal(-EINVAL, rc);
	rc = infuse_littlefs_fs_info(&fs_state);
	zassert_equal(-EINVAL, rc);
	rc = infuse_littlefs_file_open(INFUSE_LFS_FOLDER_A_GNSS, 3);
	zassert_equal(-EINVAL, rc);
	rc = infuse_littlefs_file_write(buffer, sizeof(buffer));
	zassert_equal(-EINVAL, rc);
	rc = infuse_littlefs_file_delete(INFUSE_LFS_FOLDER_A_GNSS, 3);
	zassert_equal(-EINVAL, rc);
	rc = infuse_littlefs_file_close();
	zassert_equal(0, rc);
}

ZTEST(infuse_littlefs, test_create_duplicate)
{
	struct infuse_littlefs_metadata meta = {0};
	int rc;

	/* Init filesystem */
	rc = infuse_littlefs_init();
	zassert_equal(0, rc);

	/* Create a file */
	rc = infuse_littlefs_file_create(INFUSE_LFS_FOLDER_GENERAL, 3, &meta);
	zassert_equal(0, rc);
	rc = infuse_littlefs_file_close();
	zassert_equal(0, rc);

	/* Try to create it again */
	rc = infuse_littlefs_file_create(INFUSE_LFS_FOLDER_GENERAL, 3, &meta);
	zassert_equal(-EEXIST, rc);
	rc = infuse_littlefs_file_close();
	zassert_equal(-EINVAL, rc);
}

static void file_created(enum infuse_littlefs_folder folder, uint32_t file,
			 struct infuse_littlefs_metadata *meta, void *user_data)
{
	int *count = user_data;

	zassert_not_null(user_data);
	zassert_not_null(meta);
	zassert_equal(INFUSE_LFS_FOLDER_A_GNSS, folder);
	zassert_equal(0, file);

	zassert_equal(123, meta->timestamp);
	zassert_equal(0x123, meta->identifier);
	zassert_equal(1000, meta->crc);

	*count += 1;
}

static void file_deleted(enum infuse_littlefs_folder folder, uint32_t file, void *user_data)
{
	int *count = user_data;

	zassert_not_null(user_data);
	zassert_equal(INFUSE_LFS_FOLDER_A_GNSS, folder);
	zassert_equal(0, file);

	*count += 1;
}

ZTEST(infuse_littlefs, test_standard_flow)
{
	struct infuse_littlefs_metadata meta = {
		.timestamp = 123,
		.identifier = 0x123,
		.crc = 1000,
	};
	struct infuse_littlefs_cb cb = {
		.file_created = file_created,
		.file_deleted = file_deleted,
	};
	struct infuse_littlefs_metadata meta_read;
	struct infuse_littlefs_fs_info info;
	uint8_t readback_buffer[8];
	int cb_count = 0;
	uint32_t prev_blocks;
	uint32_t crc;
	int rc;

	zassert_equal(0, sizeof(test_file_contents) % sizeof(readback_buffer));

	/* Init filesystem */
	rc = infuse_littlefs_init();
	zassert_equal(0, rc);

	/* Initial filesystem state */
	rc = infuse_littlefs_fs_info(&info);
	zassert_equal(0, rc);
	zassert_equal(LFS_DISK_VERSION, info.disk_version);
	zassert_true(info.block_size > 0);
	zassert_true(info.block_count > 0);
	zassert_true(info.blocks_used > 0);
	prev_blocks = info.blocks_used;

	/* Register for callbacks */
	cb.user_data = &cb_count;
	infuse_littlefs_register_cb(&cb);

	rc = infuse_littlefs_file_size(INFUSE_LFS_FOLDER_A_GNSS, 0);
	zassert_equal(-ENOENT, rc);
	rc = infuse_littlefs_file_metadata(INFUSE_LFS_FOLDER_A_GNSS, 0, &meta_read);
	zassert_equal(-ENOENT, rc);
	rc = infuse_littlefs_file_crc32(INFUSE_LFS_FOLDER_A_GNSS, 0, UINT32_MAX, &crc,
					readback_buffer, sizeof(readback_buffer));
	zassert_equal(-ENOENT, rc);

	/* Create a file */
	rc = infuse_littlefs_file_create(INFUSE_LFS_FOLDER_A_GNSS, 0, &meta);
	zassert_equal(0, rc);
	zassert_equal(0, cb_count);

	/* Write contents */
	rc = infuse_littlefs_file_write(test_file_contents, sizeof(test_file_contents));
	zassert_equal(sizeof(test_file_contents), rc);
	zassert_equal(0, cb_count);

	/* Commit it */
	rc = infuse_littlefs_file_close();
	zassert_equal(0, rc);
	zassert_equal(1, cb_count);

	/* Should be more blocks used on the filesystem, from creating the first folders
	 * and files on disk.
	 */
	rc = infuse_littlefs_fs_info(&info);
	zassert_equal(0, rc);
	zassert_true(info.blocks_used > prev_blocks);

	/* Check file size and metadata */
	rc = infuse_littlefs_file_size(INFUSE_LFS_FOLDER_A_GNSS, 0);
	zassert_equal(sizeof(test_file_contents), rc);
	rc = infuse_littlefs_file_metadata(INFUSE_LFS_FOLDER_A_GNSS, 0, &meta_read);
	zassert_equal(0, rc);
	zassert_equal(meta.identifier, meta_read.identifier);
	zassert_equal(meta.timestamp, meta_read.timestamp);
	zassert_equal(meta.crc, meta_read.crc);
	zassert_equal(1, cb_count);

	/* Compute the CRC of the file */
	rc = infuse_littlefs_file_crc32(INFUSE_LFS_FOLDER_A_GNSS, 0, UINT32_MAX, &crc,
					readback_buffer, sizeof(readback_buffer));
	zassert_equal(0, rc);
	zassert_equal(crc32_ieee(test_file_contents, sizeof(test_file_contents)), crc);

	/* Compute the CRC of a subset of the file */
	rc = infuse_littlefs_file_crc32(INFUSE_LFS_FOLDER_A_GNSS, 0, 13, &crc, readback_buffer,
					sizeof(readback_buffer));
	zassert_equal(0, rc);
	zassert_equal(crc32_ieee(test_file_contents, 13), crc);

	/* Open file to read back data */
	rc = infuse_littlefs_file_open(INFUSE_LFS_FOLDER_A_GNSS, 0);
	zassert_equal(0, rc);
	zassert_equal(1, cb_count);

	/* Read back data and validate */
	for (off_t offset = 0; offset < sizeof(test_file_contents);
	     offset += sizeof(readback_buffer)) {
		rc = infuse_littlefs_file_read(readback_buffer, sizeof(readback_buffer));
		zassert_equal(sizeof(readback_buffer), rc);
		zassert_mem_equal(test_file_contents + offset, readback_buffer,
				  sizeof(readback_buffer));
	}
	rc = infuse_littlefs_file_read(readback_buffer, sizeof(readback_buffer));
	zassert_equal(0, rc);
	zassert_equal(1, cb_count);

	/* Seek back to some offset */
	rc = infuse_littlefs_file_seek(4);
	zassert_equal(4, rc);
	rc = infuse_littlefs_file_read(readback_buffer, sizeof(readback_buffer));
	zassert_equal(sizeof(readback_buffer), rc);
	zassert_mem_equal(test_file_contents + 4, readback_buffer, sizeof(readback_buffer));

	/* Seek past end of file, reading returns nothing */
	rc = infuse_littlefs_file_seek(sizeof(test_file_contents) + 100);
	zassert_equal(sizeof(test_file_contents) + 100, rc);
	rc = infuse_littlefs_file_read(readback_buffer, sizeof(readback_buffer));
	zassert_equal(0, rc);
	zassert_equal(1, cb_count);

	/* Close file again */
	rc = infuse_littlefs_file_close();
	zassert_equal(0, rc);
	zassert_equal(1, cb_count);

	/* Delete the file */
	rc = infuse_littlefs_file_delete(INFUSE_LFS_FOLDER_A_GNSS, 0);
	zassert_equal(0, rc);
	zassert_equal(2, cb_count);

	/* Don't expect used blocks to decrease here, as the file is small enough that the overall
	 * blocks used stays the same.
	 */

	/* Can't be opened again */
	rc = infuse_littlefs_file_open(INFUSE_LFS_FOLDER_A_GNSS, 0);
	zassert_equal(-ENOENT, rc);
}

ZTEST(infuse_littlefs, test_auto_garbage_collection)
{
	struct infuse_littlefs_metadata meta = {
		.timestamp = 123,
		.identifier = 0x123,
		.crc = 1000,
	};
	struct infuse_littlefs_fs_info info;
	uint32_t prev_blocks;
	int rc;

	/* Init filesystem */
	rc = infuse_littlefs_init();
	zassert_equal(0, rc);

	/* Continuously creating and deleting files should work */
	for (int i = 0; i < 100; i++) {
		/* Create file on disk */
		rc = infuse_littlefs_file_create(INFUSE_LFS_FOLDER_GENERAL, i, &meta);
		zassert_equal(0, rc);
		rc = infuse_littlefs_file_write(test_file_contents, sizeof(test_file_contents));
		zassert_equal(sizeof(test_file_contents), rc);
		rc = infuse_littlefs_file_close();
		zassert_equal(0, rc);

		/* Delete it */
		rc = infuse_littlefs_file_delete(INFUSE_LFS_FOLDER_GENERAL, i);
		zassert_equal(0, rc);
	}

	rc = infuse_littlefs_fs_info(&info);
	zassert_equal(0, rc);
	prev_blocks = info.blocks_used;

	/* Write a large file to check the used block behaviour on deletion */
	rc = infuse_littlefs_file_create(INFUSE_LFS_FOLDER_GENERAL, 0, &meta);
	zassert_equal(0, rc);
	for (int i = 0; i < 4096; i += sizeof(test_file_contents)) {
		rc = infuse_littlefs_file_write(test_file_contents, sizeof(test_file_contents));
		zassert_equal(sizeof(test_file_contents), rc);
	}
	rc = infuse_littlefs_file_close();
	zassert_equal(0, rc);

	/* Blocks used with large file on disk */
	rc = infuse_littlefs_fs_info(&info);
	zassert_equal(0, rc);
	zassert_true(info.blocks_used > prev_blocks);
	prev_blocks = info.blocks_used;

	/* Delete the large file */
	rc = infuse_littlefs_file_delete(INFUSE_LFS_FOLDER_GENERAL, 0);
	zassert_equal(0, rc);

	/* Blocks used should have decreased */
	rc = infuse_littlefs_fs_info(&info);
	zassert_equal(0, rc);
	zassert_true(prev_blocks > info.blocks_used);
}

ZTEST(infuse_littlefs, test_delayed_metadata)
{
	struct infuse_littlefs_metadata meta = {
		.timestamp = 123,
		.identifier = 0x123,
		.crc = 1000,
	};
	struct infuse_littlefs_metadata meta_read;
	int rc;

	/* Init filesystem */
	rc = infuse_littlefs_init();
	zassert_equal(0, rc);

	/* Create a file */
	rc = infuse_littlefs_file_create(INFUSE_LFS_FOLDER_A_GNSS, 0, &meta);
	zassert_equal(0, rc);

	/* Change metadata after create call */
	meta.timestamp = 123456;
	meta.identifier = 0xFFFF;
	meta.crc = 0x1234;

	/* Commit it */
	rc = infuse_littlefs_file_close();
	zassert_equal(0, rc);

	/* Committed metadata should be the updated fields */
	rc = infuse_littlefs_file_metadata(INFUSE_LFS_FOLDER_A_GNSS, 0, &meta_read);
	zassert_equal(0, rc);
	zassert_equal(meta.identifier, meta_read.identifier);
	zassert_equal(meta.timestamp, meta_read.timestamp);
	zassert_equal(meta.crc, meta_read.crc);
}

static bool folder_cb_alg(enum infuse_littlefs_folder folder, uint32_t file, void *user_data)
{
	struct infuse_littlefs_metadata meta;
	int *cb_count = user_data;
	int rc;

	zassert_equal(INFUSE_LFS_FOLDER_ALGORITHMS, folder);
	zassert_not_null(user_data);

	/* Metadata can be read from inside the callback */
	rc = infuse_littlefs_file_metadata(folder, file, &meta);
	zassert_equal(0, rc);
	zassert_equal(file + 1, meta.identifier);
	zassert_equal(0, meta.timestamp);
	zassert_equal(0, meta.crc);

	*cb_count += 1;
	return true;
}

static bool folder_cb_copy(enum infuse_littlefs_folder folder, uint32_t file, void *user_data)
{
	zassert_equal(INFUSE_LFS_FOLDER_COPY, folder);
	zassert_not_null(user_data);

	*(int *)user_data += 1;
	return true;
}

static bool folder_cb_general(enum infuse_littlefs_folder folder, uint32_t file, void *user_data)
{
	zassert_not_null(user_data);
	*(int *)user_data += 1;
	return true;
}

static bool folder_cb_alg_exit(enum infuse_littlefs_folder folder, uint32_t file, void *user_data)
{
	zassert_equal(INFUSE_LFS_FOLDER_ALGORITHMS, folder);
	zassert_not_null(user_data);
	*(int *)user_data += 1;
	return false;
}

ZTEST(infuse_littlefs, test_directory_iter)
{
	struct infuse_littlefs_metadata meta = {0};
	uint32_t file;
	int cb_count;
	int rc;

	/* Init filesystem */
	rc = infuse_littlefs_init();
	zassert_equal(0, rc);

	/* Create a number of files in algorithm folder */
	for (int i = 0; i < 5; i++) {
		file = 100 + i;
		meta.identifier = file + 1;
		rc = infuse_littlefs_file_create(INFUSE_LFS_FOLDER_ALGORITHMS, file, &meta);
		zassert_equal(0, rc);
		rc = infuse_littlefs_file_write(test_file_contents, 10 + i);
		zassert_equal(10 + i, rc);
		rc = infuse_littlefs_file_close();
		zassert_equal(0, rc);
	}

	/* And a single file in copy folder */
	rc = infuse_littlefs_file_create(INFUSE_LFS_FOLDER_COPY, 200, &meta);
	zassert_equal(0, rc);
	rc = infuse_littlefs_file_write(test_file_contents, sizeof(test_file_contents));
	zassert_equal(sizeof(test_file_contents), rc);
	rc = infuse_littlefs_file_close();
	zassert_equal(0, rc);

	cb_count = 0;
	infuse_littlefs_folder_iter(INFUSE_LFS_FOLDER_ALGORITHMS, folder_cb_alg, &cb_count);
	zassert_equal(5, cb_count);

	cb_count = 0;
	infuse_littlefs_folder_iter(INFUSE_LFS_FOLDER_ALGORITHMS, folder_cb_alg_exit, &cb_count);
	zassert_equal(1, cb_count);

	cb_count = 0;
	infuse_littlefs_folder_iter(INFUSE_LFS_FOLDER_COPY, folder_cb_copy, &cb_count);
	zassert_equal(1, cb_count);

	cb_count = 0;
	infuse_littlefs_folder_iter(INFUSE_LFS_FOLDER_GENERAL, folder_cb_general, &cb_count);
	zassert_equal(0, cb_count);
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

static void test_after(void *fixture)
{
	infuse_littlefs_reset();
}

ZTEST_SUITE(infuse_littlefs, test_data_init, NULL, test_before, test_after, NULL);
