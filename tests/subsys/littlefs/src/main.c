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

#include <infuse/fs/littlefs.h>

static uint8_t *flash_buffer;
static size_t flash_buffer_size;

BUILD_ASSERT(IS_ENABLED(CONFIG_INFUSE_LITTLEFS), "LittleFS integration not enabled by default");

ZTEST(infuse_littlefs, test_1)
{
	int ret;

	/* First mount works */
	ret = infuse_littlefs_init();
	zassert_equal(0, ret);

	/* File that doesn't exist in general folder */
	ret = infuse_littlefs_file_size(INFUSE_LFS_FOLDER_GENERAL, "test.bin");
	zassert_equal(-ENOENT, ret);

	/* Second mount works as well */
	ret = infuse_littlefs_init();
	zassert_equal(0, ret);
}

static bool test_data_init(const void *global_state)
{
	flash_buffer = flash_simulator_get_memory(DEVICE_DT_GET(DT_NODELABEL(sim_flash)),
						  &flash_buffer_size);
	return true;
}

ZTEST_SUITE(infuse_littlefs, test_data_init, NULL, NULL, NULL, NULL);
