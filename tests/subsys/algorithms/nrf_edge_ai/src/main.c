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

static const uint8_t test_algorithm[] __aligned(sizeof(void *)) = {
#include "test_algorithm.inc"
};

ZTEST(algorithm_runner_llext, test_loading)
{
	const struct zbus_channel *chan = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY);
	const struct algorithm_common_config *cfg;
	__maybe_unused int rc;

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

	struct tdf_battery_state battery = {.voltage_mv = 3700, .current_ua = -100, .soc = 70};

	zbus_chan_pub(chan, &battery, K_FOREVER);

	/* Validate exported configuration */
	zassert_equal(ALGORITHM_ID_EXPECTED, cfg->algorithm_id);
	zassert_equal(ALGORITHM_ZBUS_EXPECTED, cfg->zbus_channel);
	zassert_not_null(cfg->fn);

	/* Initialise state */
	cfg->fn(NULL);

	/* Run the function many times to trigger inference */
	for (int i = 0; i < 500; i++) {
		zassert_equal(0, zbus_chan_claim(chan, K_NO_WAIT));
		cfg->fn(chan);
	}

	/* Unload the ELF */
	rc = llext_unload(&ext);
	zassert_equal(0, rc);
}

ZTEST_SUITE(algorithm_runner_llext, NULL, NULL, NULL, NULL, NULL);
