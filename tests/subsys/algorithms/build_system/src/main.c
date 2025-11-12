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
static uint8_t test_algorithm[] __aligned(sizeof(void *)) = {
#include "test_algorithm.inc"
};
#endif /* CONFIG_TEST_ALGORITHM_BUILD_LLEXT */

ZTEST(algorithm_runner_llext, test_loading)
{
	const struct zbus_channel *chan = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY);
	const struct algorithm_common_config *cfg;
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

	/* Find the configuration struct that we expect to be exported */
	cfg = llext_find_sym(&ext->exp_tab, "algorithm_config");
	zassert_not_null(cfg);
#endif /* CONFIG_TEST_ALGORITHM_BUILD_LLEXT */

#ifdef CONFIG_TEST_ALGORITHM_BUILD_NATIVE
	extern const struct algorithm_common_config test_algorithm_config;

	cfg = &test_algorithm_config;
#endif /* CONFIG_TEST_ALGORITHM_BUILD_NATIVE */

	/* Validate exported configuration */
	zassert_equal(ALGORITHM_ID_EXPECTED, cfg->algorithm_id);
	zassert_equal(ALGORITHM_ZBUS_EXPECTED, cfg->zbus_channel);
	zassert_not_null(cfg->fn);

	/* Initialise state */
	cfg->fn(NULL);

	/* Run the function a few times */
	zassert_equal(0, zbus_chan_claim(chan, K_NO_WAIT));
	cfg->fn(chan);
	zassert_equal(0, zbus_chan_claim(chan, K_NO_WAIT));
	cfg->fn(chan);
	zassert_equal(0, zbus_chan_claim(chan, K_NO_WAIT));
	cfg->fn(chan);

#ifdef CONFIG_TEST_ALGORITHM_BUILD_LLEXT
	/* Unload the ELF */
	rc = llext_unload(&ext);
	zassert_equal(0, rc);
#endif /* CONFIG_TEST_ALGORITHM_BUILD_LLEXT */
}

ZTEST_SUITE(algorithm_runner_llext, NULL, NULL, NULL, NULL, NULL);
