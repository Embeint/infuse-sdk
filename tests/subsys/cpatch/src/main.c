/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdio.h>
#include <stdbool.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>

#include <infuse/cpatch/patch.h>

#define ORIG_PARTITION  slot0_partition
#define NEW_PARTITION   slot1_partition
#define PATCH_PARTITION scratch_partition

static uint8_t file_scratch[1024];
static uint8_t area_scratch[1024];
static uint8_t output_scratch[512];

static void write_file_to_flash_area(FILE *file, const struct flash_area *fa)
{
	off_t offset = 0;
	size_t bytes_read;

	zassert_equal(0, flash_area_erase(fa, 0, fa->fa_size));

	while (true) {
		bytes_read = fread(file_scratch, 1, sizeof(file_scratch), file);
		zassert_true(bytes_read >= 0);

		zassert_equal(0, flash_area_write(fa, offset, file_scratch, bytes_read));
		offset += bytes_read;

		if (bytes_read < sizeof(file_scratch)) {
			break;
		}
	}
}

static void file_matches_flash_area(FILE *file, const struct flash_area *fa)
{
	size_t bytes_read;
	off_t offset = 0;

	while (true) {
		/* Read from file and flash area */
		bytes_read = fread(file_scratch, 1, sizeof(file_scratch), file);
		zassert_true(bytes_read >= 0);
		zassert_equal(0, flash_area_read(fa, offset, area_scratch, bytes_read));

		/* Validate contents match */
		zassert_mem_equal(file_scratch, area_scratch, bytes_read,
				  "Contents differ in chunk 0x%08X", offset);

		if (bytes_read < sizeof(file_scratch)) {
			/* File done */
			break;
		}
		offset += bytes_read;
	}
}

static void test_file_setup(const char *original, const char *patch)
{
	FILE *f_original, *f_patch;
	const struct flash_area *fa_original, *fa_patch;

	f_original = fopen(original, "rb");
	zassert_not_null(f_original);
	f_patch = fopen(patch, "rb");
	zassert_not_null(f_patch);

	/* Write input files to a flash area */
	zassert_equal(0, flash_area_open(FIXED_PARTITION_ID(ORIG_PARTITION), &fa_original));
	zassert_equal(0, flash_area_open(FIXED_PARTITION_ID(PATCH_PARTITION), &fa_patch));

	write_file_to_flash_area(f_original, fa_original);
	write_file_to_flash_area(f_patch, fa_patch);

	/* Cleanup files */
	flash_area_close(fa_original);
	flash_area_close(fa_patch);
	zassert_equal(0, fclose(f_original));
	zassert_equal(0, fclose(f_patch));
}

static void flash_area_corrupt(uint8_t flash_area_id, uint32_t offset)
{
	const struct flash_area *fa;
	uint8_t byte, readback;

	zassert_equal(0, flash_area_open(flash_area_id, &fa));

	zassert_equal(0, flash_area_read(fa, offset, &byte, 1));
	byte += 3;
	zassert_equal(0, flash_area_write(fa, offset, &byte, 1));
	zassert_equal(0, flash_area_read(fa, offset, &readback, 1));

	zassert_equal(byte, readback);

	flash_area_close(fa);
}

static void test_output_validate(const char *output)
{
	const struct flash_area *fa_new;
	FILE *f_new;

	/* Validate output matches expected file */
	f_new = fopen(output, "rb");
	zassert_equal(0, flash_area_open(FIXED_PARTITION_ID(NEW_PARTITION), &fa_new));

	file_matches_flash_area(f_new, fa_new);

	flash_area_close(fa_new);
	zassert_equal(0, fclose(f_new));
}

static int progress_cb_cnt;

static void cpatch_progress_cb(uint32_t progress_offset)
{
	progress_cb_cnt += 1;
}

static int test_binary_patch(bool small_output, bool callback)
{
	const struct flash_area *fa_original, *fa_patch;
	struct stream_flash_ctx output_ctx;
	struct cpatch_header header;
	size_t output_size;
	int rc;

	progress_cb_cnt = 0;

	/* Write input files to a flash area */
	zassert_equal(0, flash_area_open(FIXED_PARTITION_ID(ORIG_PARTITION), &fa_original));
	zassert_equal(0, flash_area_open(FIXED_PARTITION_ID(PATCH_PARTITION), &fa_patch));

	output_size = small_output ? 4096 : FIXED_PARTITION_SIZE(NEW_PARTITION);
	rc = stream_flash_init(&output_ctx, FIXED_PARTITION_DEVICE(NEW_PARTITION), output_scratch,
			       sizeof(output_scratch), FIXED_PARTITION_OFFSET(NEW_PARTITION),
			       output_size, NULL);
	zassert_equal(0, rc);

	/* Run patch */
	rc = cpatch_patch_start(fa_original, fa_patch, &header);
	if (rc < 0) {
		goto cleanup;
	}
	rc = cpatch_patch_apply(fa_original, fa_patch, &output_ctx, &header,
				callback ? cpatch_progress_cb : NULL);

	if (rc == 0 && callback) {
		zassert_true(progress_cb_cnt > 0);
	}

cleanup:
	/* Cleanup files */
	flash_area_close(fa_original);
	flash_area_close(fa_patch);

	return rc;
}

ZTEST(cpatch, test_hello_world_to_hello_world)
{
	test_file_setup(STRINGIFY(BIN_HELLO_WORLD), STRINGIFY(PATCH_HELLO_TO_HELLO));
	zassert_equal(0, test_binary_patch(false, false));
	test_output_validate(STRINGIFY(BIN_HELLO_WORLD));
}

ZTEST(cpatch, test_hello_world_validation)
{
	test_file_setup(STRINGIFY(BIN_HELLO_WORLD), STRINGIFY(PATCH_HELLO_VALIDATION));
	zassert_equal(0, test_binary_patch(false, false));
	test_output_validate(STRINGIFY(BIN_HELLO_WORLD));
}

ZTEST(cpatch, test_hello_world_invalid)
{
	test_file_setup(STRINGIFY(BIN_HELLO_WORLD), STRINGIFY(PATCH_HELLO_BAD_LEN));
	zassert_equal(-EINVAL, test_binary_patch(false, false));
	test_file_setup(STRINGIFY(BIN_HELLO_WORLD), STRINGIFY(PATCH_HELLO_BAD_CRC));
	zassert_equal(-EINVAL, test_binary_patch(false, false));
}

ZTEST(cpatch, test_hello_world_to_philosophers)
{
	test_file_setup(STRINGIFY(BIN_HELLO_WORLD), STRINGIFY(PATCH_HELLO_TO_PHILO));
	zassert_equal(0, test_binary_patch(false, true));
	test_output_validate(STRINGIFY(BIN_PHILOSOPHERS));
}

ZTEST(cpatch, test_philosophers_to_hello_world)
{
	test_file_setup(STRINGIFY(BIN_PHILOSOPHERS), STRINGIFY(PATCH_PHILO_TO_HELLO));
	zassert_equal(0, test_binary_patch(false, true));
	test_output_validate(STRINGIFY(BIN_HELLO_WORLD));
}

ZTEST(cpatch, test_output_overrun)
{
	test_file_setup(STRINGIFY(BIN_PHILOSOPHERS), STRINGIFY(PATCH_PHILO_TO_HELLO));
	zassert_not_equal(0, test_binary_patch(true, false));
	test_file_setup(STRINGIFY(BIN_HELLO_WORLD), STRINGIFY(PATCH_HELLO_TO_HELLO));
	zassert_not_equal(0, test_binary_patch(true, false));
}

ZTEST(cpatch, test_data_corruption)
{
	/* Corrupt various parts of the header */
	for (int i = 0; i < 32; i++) {
		test_file_setup(STRINGIFY(BIN_HELLO_WORLD), STRINGIFY(PATCH_HELLO_TO_PHILO));
		flash_area_corrupt(FIXED_PARTITION_ID(PATCH_PARTITION), i);
		zassert_equal(-EINVAL, test_binary_patch(false, false));
	}

	/* Corrupt various parts of the patch file */
	for (int i = 0; i < 32; i++) {
		test_file_setup(STRINGIFY(BIN_HELLO_WORLD), STRINGIFY(PATCH_HELLO_TO_PHILO));
		flash_area_corrupt(FIXED_PARTITION_ID(PATCH_PARTITION), 32 + (3 * i));
		zassert_equal(-EINVAL, test_binary_patch(false, false));
	}

	/* Corrupt various parts of the original file */
	for (int i = 0; i < 32; i++) {
		test_file_setup(STRINGIFY(BIN_HELLO_WORLD), STRINGIFY(PATCH_HELLO_TO_PHILO));
		flash_area_corrupt(FIXED_PARTITION_ID(ORIG_PARTITION), (5 * i));
		zassert_equal(-EINVAL, test_binary_patch(false, false));
	}
}

ZTEST_SUITE(cpatch, NULL, NULL, NULL, NULL, NULL);
