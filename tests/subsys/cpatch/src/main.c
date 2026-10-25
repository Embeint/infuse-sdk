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
#include <zephyr/sys/crc.h>

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
	zassert_equal(0, flash_area_open(PARTITION_ID(ORIG_PARTITION), &fa_original));
	zassert_equal(0, flash_area_open(PARTITION_ID(PATCH_PARTITION), &fa_patch));

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
	zassert_equal(0, flash_area_open(PARTITION_ID(NEW_PARTITION), &fa_new));

	file_matches_flash_area(f_new, fa_new);

	flash_area_close(fa_new);
	zassert_equal(0, fclose(f_new));
}

static int progress_cb_cnt;

static void cpatch_progress_cb(uint32_t progress, uint32_t total)
{
	ARG_UNUSED(progress);
	ARG_UNUSED(total);

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
	zassert_equal(0, flash_area_open(PARTITION_ID(ORIG_PARTITION), &fa_original));
	zassert_equal(0, flash_area_open(PARTITION_ID(PATCH_PARTITION), &fa_patch));

	output_size = small_output ? 4096 : PARTITION_SIZE(NEW_PARTITION);
	rc = stream_flash_init(&output_ctx, PARTITION_DEVICE(NEW_PARTITION), output_scratch,
			       sizeof(output_scratch), PARTITION_OFFSET(NEW_PARTITION), output_size,
			       NULL);
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

ZTEST(cpatch, test_hello_world_validation_delta)
{
	test_file_setup(STRINGIFY(BIN_HELLO_WORLD), STRINGIFY(PATCH_HELLO_VALIDATION_DELTA));
	zassert_equal(0, test_binary_patch(false, false));
	test_output_validate(STRINGIFY(BIN_HELLO_WORLD));
}

ZTEST(cpatch, test_hello_world_invalid)
{
	test_file_setup(STRINGIFY(BIN_HELLO_WORLD), STRINGIFY(PATCH_HELLO_BAD_LEN));
	zassert_equal(-EINVAL, test_binary_patch(false, false));
	test_file_setup(STRINGIFY(BIN_HELLO_WORLD), STRINGIFY(PATCH_HELLO_BAD_CRC));
	zassert_equal(-EINVAL, test_binary_patch(false, false));
	test_file_setup(STRINGIFY(BIN_HELLO_WORLD), STRINGIFY(PATCH_HELLO_BAD_LEN_DELTA));
	zassert_equal(-EINVAL, test_binary_patch(false, false));
	test_file_setup(STRINGIFY(BIN_HELLO_WORLD), STRINGIFY(PATCH_HELLO_BAD_CRC_DELTA));
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

ZTEST(cpatch, test_hello_world_to_philosophers_delta)
{
	test_file_setup(STRINGIFY(BIN_HELLO_WORLD), STRINGIFY(PATCH_HELLO_TO_PHILO_DELTA));
	zassert_equal(0, test_binary_patch(false, true));
	test_output_validate(STRINGIFY(BIN_PHILOSOPHERS));
}

ZTEST(cpatch, test_philosophers_to_hello_world_delta)
{
	test_file_setup(STRINGIFY(BIN_PHILOSOPHERS), STRINGIFY(PATCH_PHILO_TO_HELLO_DELTA));
	zassert_equal(0, test_binary_patch(false, true));
	test_output_validate(STRINGIFY(BIN_HELLO_WORLD));
}

/* The stream patches relocate pointers into a RAM region */
#if defined(CONFIG_INFUSE_CPATCH_ENCODER_STREAM_THUMB2) &&                                         \
	CONFIG_INFUSE_CPATCH_STREAM_THUMB2_REGIONS_MAX > 0
#define STREAM_EXPECTED 0
#else
#define STREAM_EXPECTED -EINVAL
#endif

ZTEST(cpatch, test_hello_world_to_philosophers_stream_thumb2)
{
	test_file_setup(STRINGIFY(BIN_HELLO_WORLD), STRINGIFY(PATCH_HELLO_TO_PHILO_STREAM_THUMB2));
	zassert_equal(STREAM_EXPECTED, test_binary_patch(false, true));
	if (STREAM_EXPECTED == 0) {
		test_output_validate(STRINGIFY(BIN_PHILOSOPHERS));
	}
}

ZTEST(cpatch, test_philosophers_to_hello_world_stream_thumb2)
{
	test_file_setup(STRINGIFY(BIN_PHILOSOPHERS), STRINGIFY(PATCH_PHILO_TO_HELLO_STREAM_THUMB2));
	zassert_equal(STREAM_EXPECTED, test_binary_patch(false, true));
	if (STREAM_EXPECTED == 0) {
		test_output_validate(STRINGIFY(BIN_HELLO_WORLD));
	}
}

#ifdef CONFIG_INFUSE_CPATCH_ENCODER_STREAM_THUMB2

#define STREAM_INPUT_LEN 64

/* Apply stream patch data with valid CRCs to a known input file */
static int test_stream_patch(const uint8_t *data, size_t len, const uint8_t *expected,
			     size_t expected_len)
{
	const struct flash_area *fa_original, *fa_patch;
	uint8_t input[STREAM_INPUT_LEN];
	struct stream_flash_ctx output_ctx;
	struct cpatch_header header = {
		.magic_value = CPATCH_MAGIC_NUMBER,
		.version_major = 3,
		.input_file = {.length = sizeof(input)},
		.output_file = {.length = expected_len, .crc = crc32_ieee(expected, expected_len)},
		.patch_file = {.length = len, .crc = crc32_ieee(data, len)},
	};
	int rc;

	for (int i = 0; i < sizeof(input); i++) {
		input[i] = i;
	}
	header.input_file.crc = crc32_ieee(input, sizeof(input));
	header.header_crc = crc32_ieee((void *)&header, sizeof(header) - sizeof(uint32_t));

	zassert_equal(0, flash_area_open(PARTITION_ID(ORIG_PARTITION), &fa_original));
	zassert_equal(0, flash_area_open(PARTITION_ID(PATCH_PARTITION), &fa_patch));
	zassert_equal(0, flash_area_erase(fa_original, 0, fa_original->fa_size));
	zassert_equal(0, flash_area_erase(fa_patch, 0, fa_patch->fa_size));
	zassert_equal(0, flash_area_write(fa_original, 0, input, sizeof(input)));
	zassert_equal(0, flash_area_write(fa_patch, 0, &header, sizeof(header)));
	zassert_equal(0, flash_area_write(fa_patch, sizeof(header), data, len));

	zassert_equal(0, stream_flash_init(&output_ctx, PARTITION_DEVICE(NEW_PARTITION),
					   output_scratch, sizeof(output_scratch),
					   PARTITION_OFFSET(NEW_PARTITION),
					   PARTITION_SIZE(NEW_PARTITION), NULL));

	/* Patch must pass validation, so that only the element stream can be rejected */
	zassert_equal(0, cpatch_patch_start(fa_original, fa_patch, &header));
	rc = cpatch_patch_apply(fa_original, fa_patch, &output_ctx, &header, NULL);

	flash_area_close(fa_original);
	flash_area_close(fa_patch);
	return rc;
}

ZTEST(cpatch, test_stream_thumb2_valid)
{
	/* No relocation, copy 8 bytes, then a single literal byte */
	const uint8_t patch[] = {0x00, 0x8F, 0x0B, 0xAA};
	const uint8_t expected[] = {0, 1, 2, 3, 4, 5, 6, 7, 0xAA};

	zassert_equal(0, test_stream_patch(patch, sizeof(patch), expected, sizeof(expected)));
}

ZTEST(cpatch, test_stream_thumb2_malformed)
{
	const struct {
		const char *name;
		uint8_t data[16];
		size_t len;
	} cases[] = {
		{"unsupported flags", {0x04}, 1},
		{"map past patch end", {0x01, 0xFF, 0xFF}, 3},
#if CONFIG_INFUSE_CPATCH_STREAM_THUMB2_REGIONS_MAX < 255
		{"too many regions",
		 {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  CONFIG_INFUSE_CPATCH_STREAM_THUMB2_REGIONS_MAX + 1},
		 8},
#endif
		{"copy past input end", {0x00, 0xFF, 0x40}, 3},
		{"copy after shift past input end", {0x00, 0x0E, 0x80, 0x01, 0x1F}, 5},
		{"copy after shift before input start", {0x00, 0x0E, 0x01, 0x1F}, 4},
		{"add past input end", {0x00, 0x0E, 0x7E, 0x00}, 4},
		{"extended operation", {0x00, 0x0F}, 2},
		{"varint too long", {0x00, 0xFF, 0x80, 0x80, 0x80, 0x80, 0x80}, 7},
		{"literal past patch end", {0x00, 0x0C, 0x10}, 3},
	};

	for (int i = 0; i < ARRAY_SIZE(cases); i++) {
		zassert_equal(-EINVAL, test_stream_patch(cases[i].data, cases[i].len, NULL, 0),
			      "%s", cases[i].name);
	}
}

#endif /* CONFIG_INFUSE_CPATCH_ENCODER_STREAM_THUMB2 */

ZTEST(cpatch, test_output_overrun)
{
	test_file_setup(STRINGIFY(BIN_PHILOSOPHERS), STRINGIFY(PATCH_PHILO_TO_HELLO));
	zassert_not_equal(0, test_binary_patch(true, false));
	test_file_setup(STRINGIFY(BIN_HELLO_WORLD), STRINGIFY(PATCH_HELLO_TO_HELLO));
	zassert_not_equal(0, test_binary_patch(true, false));
	test_file_setup(STRINGIFY(BIN_PHILOSOPHERS), STRINGIFY(PATCH_PHILO_TO_HELLO_DELTA));
	zassert_not_equal(0, test_binary_patch(true, false));
	test_file_setup(STRINGIFY(BIN_PHILOSOPHERS), STRINGIFY(PATCH_PHILO_TO_HELLO_STREAM_THUMB2));
	zassert_not_equal(0, test_binary_patch(true, false));
}

ZTEST(cpatch, test_data_corruption)
{
	/* Corrupt various parts of the header */
	for (int i = 0; i < 32; i++) {
		test_file_setup(STRINGIFY(BIN_HELLO_WORLD), STRINGIFY(PATCH_HELLO_TO_PHILO));
		flash_area_corrupt(PARTITION_ID(PATCH_PARTITION), i);
		zassert_equal(-EINVAL, test_binary_patch(false, false));
	}

	/* Corrupt various parts of the patch file */
	for (int i = 0; i < 32; i++) {
		test_file_setup(STRINGIFY(BIN_HELLO_WORLD), STRINGIFY(PATCH_HELLO_TO_PHILO));
		flash_area_corrupt(PARTITION_ID(PATCH_PARTITION), 32 + (3 * i));
		zassert_equal(-EINVAL, test_binary_patch(false, false));
	}

	/* Corrupt various parts of the original file */
	for (int i = 0; i < 32; i++) {
		test_file_setup(STRINGIFY(BIN_HELLO_WORLD), STRINGIFY(PATCH_HELLO_TO_PHILO));
		flash_area_corrupt(PARTITION_ID(ORIG_PARTITION), (5 * i));
		zassert_equal(-EINVAL, test_binary_patch(false, false));
	}

	/* Corrupt various parts of a PATCH_DELTA patch file */
	for (int i = 0; i < 32; i++) {
		test_file_setup(STRINGIFY(BIN_HELLO_WORLD), STRINGIFY(PATCH_HELLO_TO_PHILO_DELTA));
		flash_area_corrupt(PARTITION_ID(PATCH_PARTITION), 32 + (3 * i));
		zassert_equal(-EINVAL, test_binary_patch(false, false));
	}

	/* Corrupt various parts of a stream patch file */
	for (int i = 0; i < 32; i++) {
		test_file_setup(STRINGIFY(BIN_HELLO_WORLD),
					  STRINGIFY(PATCH_HELLO_TO_PHILO_STREAM_THUMB2));
		flash_area_corrupt(PARTITION_ID(PATCH_PARTITION), 32 + (3 * i));
		zassert_equal(-EINVAL, test_binary_patch(false, false));
	}
}

ZTEST_SUITE(cpatch, NULL, NULL, NULL, NULL, NULL);
