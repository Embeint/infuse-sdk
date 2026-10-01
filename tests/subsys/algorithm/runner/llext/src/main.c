/*
 * Copyright (c) 2026 Embeint Holdings Pty Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/llext/llext.h>
#include <zephyr/sys/crc.h>
#include <zephyr/ztest.h>

#include <infuse/work_q.h>
#include <infuse/algorithm_runner/runner.h>
#include <infuse/fs/littlefs.h>
#include <infuse/zbus/channels.h>

#include "test_algorithm.h"

INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_AMBIENT_ENV);

static bool builtin_algorithm_initialised;
static struct k_work work_queue_blocker;
static K_SEM_DEFINE(work_queue_blocker_started, 0, 1);
static K_SEM_DEFINE(work_queue_blocker_release, 0, 1);

static void work_queue_blocker_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	k_sem_give(&work_queue_blocker_started);
	k_sem_take(&work_queue_blocker_release, K_FOREVER);
}

static void builtin_algorithm_fn(const struct zbus_channel *chan,
				 const struct infuse_algorithm *algorithm, const void *args)
{
	ARG_UNUSED(algorithm);
	ARG_UNUSED(args);

	if (chan == NULL) {
		builtin_algorithm_initialised = true;
		return;
	}

	struct tdf_ambient_temp_pres_hum *environment = (void *)zbus_chan_const_msg(chan);

	/* The host expects +4, proving initialisation ran before the channel publish. */
	environment->temperature += builtin_algorithm_initialised ? 4 : 400;
	zbus_chan_finish(chan);
}

static const struct infuse_algorithm builtin_algorithm = {
	.algorithm_id = 0xABCDEF01,
	.algorithm_version = 1,
	.zbus_channel = INFUSE_ZBUS_CHAN_AMBIENT_ENV,
	.fn = builtin_algorithm_fn,
};

static const struct infuse_algorithm older_builtin_algorithm = {
	.algorithm_id = TEST_ALGORITHM_FILE_ID,
	.algorithm_version = 1,
	.zbus_channel = INFUSE_ZBUS_CHAN_AMBIENT_ENV,
	.fn = builtin_algorithm_fn,
};

static void newer_builtin_algorithm_fn(const struct zbus_channel *chan,
				       const struct infuse_algorithm *algorithm, const void *args)
{
	ARG_UNUSED(algorithm);
	ARG_UNUSED(args);

	if (chan == NULL) {
		return;
	}

	struct tdf_ambient_temp_pres_hum *environment = (void *)zbus_chan_const_msg(chan);

	environment->temperature += 8;
	zbus_chan_finish(chan);
}

static const struct infuse_algorithm newer_builtin_algorithm = {
	.algorithm_id = TEST_ALGORITHM_FILE_ID,
	.algorithm_version = 3,
	.zbus_channel = INFUSE_ZBUS_CHAN_AMBIENT_ENV,
	.fn = newer_builtin_algorithm_fn,
};

static void expect_environment_temperature_change(int32_t change)
{
	struct tdf_ambient_temp_pres_hum environment = {
		.temperature = 1234,
	};
	struct tdf_ambient_temp_pres_hum result;

	zassert_ok(zbus_chan_pub(INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_AMBIENT_ENV), &environment,
				 K_FOREVER));
	k_sleep(K_MSEC(100));
	zassert_ok(zbus_chan_read(INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_AMBIENT_ENV), &result,
				  K_MSEC(100)));
	zassert_equal(environment.temperature + change, result.temperature);
}

ZTEST(algorithm_littlefs, test_algorithm_install)
{
	struct infuse_littlefs_metadata metadata;
	const uint8_t *expected = test_algorithm_data();
	uint8_t buffer[64];
	size_t offset = 0;
	int rc;

	rc = infuse_littlefs_init();
	zassert_ok(rc);

	algorithm_runner_init();

	/* Building the application must not automatically install the algorithm. */
	rc = infuse_littlefs_file_size(INFUSE_LFS_FOLDER_ALGORITHMS, TEST_ALGORITHM_FILE_ID);
	zassert_equal(-ENOENT, rc);

	zassert_ok(test_algorithm_install());
	zassert_equal(test_algorithm_size(), infuse_littlefs_file_size(INFUSE_LFS_FOLDER_ALGORITHMS,
								       TEST_ALGORITHM_FILE_ID));
	zassert_ok(infuse_littlefs_file_metadata(INFUSE_LFS_FOLDER_ALGORITHMS,
						 TEST_ALGORITHM_FILE_ID, &metadata));
	zassert_equal(TEST_ALGORITHM_FILE_ID, metadata.identifier);
	zassert_equal(crc32_ieee(expected, test_algorithm_size()), metadata.crc);

	zassert_ok(infuse_littlefs_file_open(INFUSE_LFS_FOLDER_ALGORITHMS, TEST_ALGORITHM_FILE_ID));
	while (offset < test_algorithm_size()) {
		size_t read_size = MIN(sizeof(buffer), test_algorithm_size() - offset);

		rc = infuse_littlefs_file_read(buffer, read_size);
		zassert_equal(read_size, rc);
		zassert_mem_equal(expected + offset, buffer, read_size);
		offset += read_size;
	}
	zassert_ok(infuse_littlefs_file_close());
}

ZTEST(algorithm_littlefs, test_runner_llext_single)
{
	/* Write algorithm */
	zassert_ok(infuse_littlefs_init());
	algorithm_runner_init();
	zassert_ok(test_algorithm_install());

	/* The runner work queue loads and registers the algorithm. */
	k_sleep(K_MSEC(100));

	/* Publishing environmental data triggers the initialised algorithm. The +1 result also
	 * verifies the runner previously called it with chan == NULL.
	 */
	expect_environment_temperature_change(1);

	/* Deleting the file unloads the extension. */
	zassert_ok(
		infuse_littlefs_file_delete(INFUSE_LFS_FOLDER_ALGORITHMS, TEST_ALGORITHM_FILE_ID));
	k_sleep(K_MSEC(100));

	/* Publishing to the environment channel doesn't trigger anything */
	expect_environment_temperature_change(0);
}

ZTEST(algorithm_littlefs, test_runner_loads_existing_llext)
{
	/* Install the algorithm before the runner starts, as occurs after a reboot. */
	zassert_ok(infuse_littlefs_init());
	zassert_ok(test_algorithm_install());

	algorithm_runner_init();

	/* The runner work queue loads and registers the existing algorithm. */
	k_sleep(K_MSEC(100));
	expect_environment_temperature_change(1);
}

ZTEST(algorithm_littlefs, test_runner_rejects_unknown_struct_version)
{
	zassert_ok(infuse_littlefs_init());
	algorithm_runner_init();
	zassert_ok(test_algorithm_invalid_version_install());

	/* The loader rejects and unloads algorithms using an unknown descriptor version. */
	k_sleep(K_MSEC(100));
	zassert_is_null(llext_by_name("12344321"));
	expect_environment_temperature_change(0);
}

ZTEST(algorithm_littlefs, test_runner_retries_multiple_llext_loads_while_filesystem_busy)
{
	zassert_ok(infuse_littlefs_init());
	algorithm_runner_init();

	/* Hold the runner work queue so the algorithm file can be opened before loading starts. */
	k_work_init(&work_queue_blocker, work_queue_blocker_fn);
	zassert_true(infuse_work_submit(&work_queue_blocker) >= 0);
	zassert_ok(k_sem_take(&work_queue_blocker_started, K_SECONDS(1)));

	zassert_ok(test_algorithm_install());
	zassert_ok(test_algorithm_2_install());
	zassert_ok(infuse_littlefs_file_open(INFUSE_LFS_FOLDER_ALGORITHMS, TEST_ALGORITHM_FILE_ID));
	k_sem_give(&work_queue_blocker_release);

	/* Keep the filesystem busy across multiple retry intervals. */
	k_sleep(K_MSEC(250));
	zassert_ok(infuse_littlefs_file_close());
	k_sleep(K_MSEC(150));

	/* Both queued algorithms load after the filesystem becomes available. */
	expect_environment_temperature_change(3);
}

ZTEST(algorithm_littlefs, test_runner_llext_multiple)
{
	zassert_ok(infuse_littlefs_init());
	algorithm_runner_init();

	/* Install and load two independent algorithms. */
	zassert_ok(test_algorithm_install());
	k_sleep(K_MSEC(100));
	zassert_ok(test_algorithm_2_install());
	k_sleep(K_MSEC(100));

	/* Both algorithms are initialised and run for the same channel publish. */
	expect_environment_temperature_change(3);

	/* Removing the first algorithm leaves the second algorithm active. */
	zassert_ok(
		infuse_littlefs_file_delete(INFUSE_LFS_FOLDER_ALGORITHMS, TEST_ALGORITHM_FILE_ID));
	k_sleep(K_MSEC(100));
	expect_environment_temperature_change(2);

	/* Removing the second algorithm leaves no subscriber active. */
	zassert_ok(infuse_littlefs_file_delete(INFUSE_LFS_FOLDER_ALGORITHMS,
					       TEST_ALGORITHM_2_FILE_ID));
	k_sleep(K_MSEC(100));
	expect_environment_temperature_change(0);
}

ZTEST(algorithm_littlefs, test_runner_builtin_and_llext)
{
	zassert_ok(infuse_littlefs_init());
	algorithm_runner_init();

	/* Register one built-in algorithm and load one filesystem algorithm. */
	builtin_algorithm_initialised = false;
	zassert_ok(algorithm_runner_register(&builtin_algorithm));
	zassert_true(builtin_algorithm_initialised);
	zassert_ok(test_algorithm_install());
	k_sleep(K_MSEC(100));

	/* Both algorithm types run for the same channel publish. */
	expect_environment_temperature_change(5);

	/* Removing the extension leaves the built-in algorithm active. */
	zassert_ok(
		infuse_littlefs_file_delete(INFUSE_LFS_FOLDER_ALGORITHMS, TEST_ALGORITHM_FILE_ID));
	k_sleep(K_MSEC(100));
	expect_environment_temperature_change(4);

	/* The built-in algorithm can still be explicitly unregistered. */
	zassert_ok(algorithm_runner_unregister(&builtin_algorithm));
	expect_environment_temperature_change(0);
}

ZTEST(algorithm_littlefs, test_runner_llext_supersedes_builtin)
{
	zassert_ok(infuse_littlefs_init());
	algorithm_runner_init();

	/* The version 2 extension supersedes the version 1 built-in with the same ID. */
	builtin_algorithm_initialised = false;
	zassert_ok(algorithm_runner_register(&older_builtin_algorithm));
	zassert_ok(test_algorithm_install());
	k_sleep(K_MSEC(100));
	expect_environment_temperature_change(1);

	/* Removing the extension restores the older built-in implementation. */
	zassert_ok(
		infuse_littlefs_file_delete(INFUSE_LFS_FOLDER_ALGORITHMS, TEST_ALGORITHM_FILE_ID));
	k_sleep(K_MSEC(100));
	expect_environment_temperature_change(4);
}

ZTEST(algorithm_littlefs, test_runner_builtin_supersedes_llext)
{
	zassert_ok(infuse_littlefs_init());
	algorithm_runner_init();

	/* The version 3 built-in supersedes the version 2 extension with the same ID. */
	zassert_ok(algorithm_runner_register(&newer_builtin_algorithm));
	zassert_ok(test_algorithm_install());
	k_sleep(K_MSEC(100));
	expect_environment_temperature_change(8);

	/* Removing the older extension does not affect the built-in implementation. */
	zassert_ok(
		infuse_littlefs_file_delete(INFUSE_LFS_FOLDER_ALGORITHMS, TEST_ALGORITHM_FILE_ID));
	k_sleep(K_MSEC(100));
	expect_environment_temperature_change(8);
}

static void test_after(void *fixture)
{
	ARG_UNUSED(fixture);
	algorithm_runner_reset();
	infuse_littfs_format();
	infuse_littlefs_reset();
}

ZTEST_SUITE(algorithm_littlefs, NULL, NULL, NULL, test_after, NULL);
