/*
 * Copyright (c) 2026 Embeint Holdings Pty Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/sys/crc.h>

#include <infuse/fs/littlefs.h>

#include "test_algorithm.h"

static const uint8_t test_algorithm[] __aligned(sizeof(void *)) = {
#include "test_algorithm.inc"
};

static const uint8_t test_algorithm_2[] __aligned(sizeof(void *)) = {
#include "test_algorithm_2.inc"
};

static const uint8_t test_algorithm_invalid_version[] __aligned(sizeof(void *)) = {
#include "test_algorithm_invalid_version.inc"
};

static int algorithm_install(uint32_t file, const uint8_t *algorithm, size_t algorithm_size)
{
	struct infuse_littlefs_metadata metadata = {
		.identifier = file,
		.crc = crc32_ieee(algorithm, algorithm_size),
	};
	int rc;

	rc = infuse_littlefs_file_create(INFUSE_LFS_FOLDER_ALGORITHMS, file, &metadata);
	if (rc < 0) {
		return rc;
	}

	rc = infuse_littlefs_file_write(algorithm, algorithm_size);
	if (rc != algorithm_size) {
		(void)infuse_littlefs_file_close();
		(void)infuse_littlefs_file_delete(INFUSE_LFS_FOLDER_ALGORITHMS, file);
		return rc < 0 ? rc : -EIO;
	}

	return infuse_littlefs_file_close();
}

int test_algorithm_install(void)
{
	return algorithm_install(TEST_ALGORITHM_FILE_ID, test_algorithm, sizeof(test_algorithm));
}

int test_algorithm_2_install(void)
{
	return algorithm_install(TEST_ALGORITHM_2_FILE_ID, test_algorithm_2,
				 sizeof(test_algorithm_2));
}

int test_algorithm_invalid_version_install(void)
{
	return algorithm_install(TEST_ALGORITHM_INVALID_VERSION_FILE_ID,
				 test_algorithm_invalid_version,
				 sizeof(test_algorithm_invalid_version));
}

const uint8_t *test_algorithm_data(void)
{
	return test_algorithm;
}

size_t test_algorithm_size(void)
{
	return sizeof(test_algorithm);
}
