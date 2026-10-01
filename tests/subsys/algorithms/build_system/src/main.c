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
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/tdf/tdf.h>
#include <infuse/zbus/channels.h>

#include "algorithm_info.h"

BUILD_ASSERT(ALGORITHM_LOGGER_EXPECTED == TDF_DATA_LOGGER_SERIAL);
BUILD_ASSERT(ALGORITHM_TDF_ID_EXPECTED == TDF_BATTERY_STATE);

INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_BATTERY);

static void validate_logged_battery(const struct tdf_battery_state *expected)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_buffer_state state;
	struct tdf_parsed tdf;
	struct net_buf *pkt;
	int count = 0;

	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	while ((pkt = k_fifo_get(tx_queue, K_MSEC(10))) != NULL) {
		net_buf_pull(pkt, sizeof(struct epacket_dummy_frame));
		tdf_parse_start(&state, pkt->data, pkt->len);
		while (tdf_parse(&state, &tdf) == 0) {
			zassert_equal(TDF_BATTERY_STATE, tdf.tdf_id);
			zassert_equal(sizeof(*expected), tdf.tdf_len);
			zassert_mem_equal(expected, tdf.data, sizeof(*expected));
			count += 1;
		}
		net_buf_unref(pkt);
	}
	zassert_equal(3, count, "Enabled mask should log once per algorithm invocation");
}

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

	/* The enabled call logs once per run; the disabled call must not log. */
	validate_logged_battery(&battery);

#ifdef CONFIG_TEST_ALGORITHM_BUILD_LLEXT
	/* Unload the ELF */
	rc = llext_unload(&ext);
	zassert_equal(0, rc);
#endif /* CONFIG_TEST_ALGORITHM_BUILD_LLEXT */
}

ZTEST_SUITE(algorithm_runner_llext, NULL, NULL, NULL, NULL, NULL);
