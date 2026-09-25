/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/llext/llext.h>
#include <zephyr/llext/buf_loader.h>

#include <infuse/algorithms/implementation.h>
#include <infuse/zbus/channels.h>

#include "algorithm_info.h"

INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_BATTERY);

#ifdef CONFIG_TEST_ALGORITHM_BUILD_LLEXT
static const uint8_t test_algorithm[] __aligned(sizeof(void *)) = {
#include "test_algorithm.inc"
};
#endif /* CONFIG_TEST_ALGORITHM_BUILD_LLEXT */

ZTEST(algorithm_runner_llext, test_loading)
{
	const struct zbus_channel *chan = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY);
	const struct infuse_algorithm *algorithm;
	__maybe_unused int rc;

#ifdef CONFIG_TEST_ALGORITHM_BUILD_LLEXT
	struct llext_buf_loader buf_loader =
		LLEXT_BUF_LOADER(test_algorithm, sizeof(test_algorithm));
	struct llext_loader *loader = &buf_loader.loader;
	struct llext_load_param ldr_parm = LLEXT_LOAD_PARAM_DEFAULT;
	struct llext *ext;

	/* Load the ELF file */
	rc = llext_load(loader, "test_alg", &ext, &ldr_parm);
	zassert_equal(0, rc);

	/* Find the algorithm struct that we expect to be exported */
	algorithm = llext_find_sym(&ext->exp_tab, "algorithm_config");
	zassert_not_null(algorithm);
#endif /* CONFIG_TEST_ALGORITHM_BUILD_LLEXT */

#ifdef CONFIG_TEST_ALGORITHM_BUILD_NATIVE
	extern const struct infuse_algorithm test_algorithm;

	algorithm = &test_algorithm;
#endif /* CONFIG_TEST_ALGORITHM_BUILD_NATIVE */

	struct tdf_battery_state battery = {.voltage_mv = 3700, .current_ua = -100, .soc = 70};

	zbus_chan_pub(chan, &battery, K_FOREVER);

	/* Validate exported algorithm */
	zassert_equal(ALGORITHM_ID_EXPECTED, algorithm->algorithm_id);
	zassert_equal(ALGORITHM_ZBUS_EXPECTED, algorithm->zbus_channel);
	zassert_not_null(algorithm->fn);

	/* Initialise state */
	algorithm->fn(NULL, algorithm, NULL);

	/* Run the function a few times */
	zassert_equal(0, zbus_chan_claim(chan, K_NO_WAIT));
	algorithm->fn(chan, algorithm, NULL);
	zassert_equal(0, zbus_chan_claim(chan, K_NO_WAIT));
	algorithm->fn(chan, algorithm, NULL);
	zassert_equal(0, zbus_chan_claim(chan, K_NO_WAIT));
	algorithm->fn(chan, algorithm, NULL);

#ifdef CONFIG_TEST_ALGORITHM_BUILD_LLEXT
	/* Unload the ELF */
	rc = llext_unload(&ext);
	zassert_equal(0, rc);
#endif /* CONFIG_TEST_ALGORITHM_BUILD_LLEXT */
}

ZTEST_SUITE(algorithm_runner_llext, NULL, NULL, NULL, NULL, NULL);
