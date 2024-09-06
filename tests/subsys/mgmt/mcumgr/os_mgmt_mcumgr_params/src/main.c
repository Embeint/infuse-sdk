/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 * Copyright (c) 2023 Jamie M.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/net/buf.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/transport/smp_dummy.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/mgmt/mcumgr/grp/os_mgmt/os_mgmt.h>
#include <zcbor_common.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include <mgmt/mcumgr/util/zcbor_bulk.h>
#include <string.h>
#include <smp_internal.h>
#include "smp_test_util.h"

#include <infuse/time/epoch.h>

#define SMP_RESPONSE_WAIT_TIME   3
#define ZCBOR_BUFFER_SIZE        256
#define OUTPUT_BUFFER_SIZE       256
#define ZCBOR_HISTORY_ARRAY_SIZE 4

static struct net_buf *nb;
struct group_error {
	uint16_t group;
	uint16_t rc;
	bool found;
};

static void cleanup_test(void *p);

static bool mcumgr_ret_decode(zcbor_state_t *state, struct group_error *result)
{
	bool ok;
	size_t decoded;
	uint32_t tmp_group;
	uint32_t tmp_rc;

	struct zcbor_map_decode_key_val output_decode[] = {
		ZCBOR_MAP_DECODE_KEY_DECODER("group", zcbor_uint32_decode, &tmp_group),
		ZCBOR_MAP_DECODE_KEY_DECODER("rc", zcbor_uint32_decode, &tmp_rc),
	};

	result->found = false;

	ok = zcbor_map_decode_bulk(state, output_decode, ARRAY_SIZE(output_decode), &decoded) == 0;

	if (ok &&
	    zcbor_map_decode_bulk_key_found(output_decode, ARRAY_SIZE(output_decode), "group") &&
	    zcbor_map_decode_bulk_key_found(output_decode, ARRAY_SIZE(output_decode), "rc")) {
		result->group = (uint16_t)tmp_group;
		result->rc = (uint16_t)tmp_rc;
		result->found = true;
	}

	return ok;
}

ZTEST(os_mgmt_mcumgr_params, test_mcumgr_params)
{
	uint8_t buffer[ZCBOR_BUFFER_SIZE];
	uint8_t buffer_out[OUTPUT_BUFFER_SIZE];
	bool ok;
	uint16_t buffer_size;
	zcbor_state_t zse[ZCBOR_HISTORY_ARRAY_SIZE] = {0};
	zcbor_state_t zsd[ZCBOR_HISTORY_ARRAY_SIZE] = {0};
	uint32_t buf_size, buf_count;
	struct group_error group;
	size_t decoded = 0;
	bool received;
	int rc;

	struct zcbor_map_decode_key_val output_decode[] = {
		ZCBOR_MAP_DECODE_KEY_DECODER("buf_size", zcbor_uint32_decode, &buf_size),
		ZCBOR_MAP_DECODE_KEY_DECODER("buf_count", zcbor_uint32_decode, &buf_count),
		ZCBOR_MAP_DECODE_KEY_DECODER("rc", zcbor_int32_decode, &rc),
		ZCBOR_MAP_DECODE_KEY_DECODER("err", mcumgr_ret_decode, &group),
	};

	memset(buffer, 0, sizeof(buffer));
	memset(buffer_out, 0, sizeof(buffer_out));
	buffer_size = 0;
	memset(zse, 0, sizeof(zse));
	memset(zsd, 0, sizeof(zsd));

	/* Query time and ensure it is set */
	zcbor_new_encode_state(zse, 2, buffer, ARRAY_SIZE(buffer), 0);
	ok = create_mcumgr_mcumgr_params_get_packet(zse, false, buffer, buffer_out, &buffer_size);
	zassert_true(ok, "Expected packet creation to be successful");

	/* Enable dummy SMP backend and ready for usage */
	smp_dummy_enable();
	smp_dummy_clear_state();

	/* Send query command to dummy SMP backend */
	(void)smp_dummy_tx_pkt(buffer_out, buffer_size);
	smp_dummy_add_data();

	/* For a short duration to see if response has been received */
	received = smp_dummy_wait_for_data(SMP_RESPONSE_WAIT_TIME);
	zassert_true(received, "Expected to receive data but timed out");

	/* Retrieve response buffer and ensure validity */
	nb = smp_dummy_get_outgoing();
	smp_dummy_disable();

	/* Process received data by removing header */
	(void)net_buf_pull(nb, sizeof(struct smp_hdr));
	zcbor_new_decode_state(zsd, 4, nb->data, nb->len, 1, NULL, 0);

	ok = zcbor_map_decode_bulk(zsd, output_decode, ARRAY_SIZE(output_decode), &decoded) == 0;
	zassert_true(ok, "Expected decode to be successful");
	zassert_equal(decoded, 2, "Expected to receive 2 decoded zcbor elements");
	zassert_true(zcbor_map_decode_bulk_key_found(output_decode, ARRAY_SIZE(output_decode),
						     "buf_size"),
		     "Expected to receive buf_size element");
	zassert_true(zcbor_map_decode_bulk_key_found(output_decode, ARRAY_SIZE(output_decode),
						     "buf_count"),
		     "Expected to receive buf_count element");
	zassert_false(
		zcbor_map_decode_bulk_key_found(output_decode, ARRAY_SIZE(output_decode), "rc"),
		"Did not expect to receive rc element");
	zassert_false(
		zcbor_map_decode_bulk_key_found(output_decode, ARRAY_SIZE(output_decode), "err"),
		"Did not expected to receive err element");

	/* Check that returned values are as expected */
	zassert_equal(CONFIG_MCUMGR_TRANSPORT_NETBUF_SIZE, buf_size);
	zassert_equal(CONFIG_MCUMGR_TRANSPORT_NETBUF_COUNT, buf_count);
}

static void cleanup_test(void *p)
{
	if (nb != NULL) {
		net_buf_unref(nb);
		nb = NULL;
	}
}

/* Time not set test set */
ZTEST_SUITE(os_mgmt_mcumgr_params, NULL, NULL, NULL, cleanup_test, NULL);
