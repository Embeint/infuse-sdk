/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/net_buf.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/transport/smp_dummy.h>
#include <zephyr/mgmt/mcumgr/grp/os_mgmt/os_mgmt.h>

#include <zcbor_common.h>
#include <zcbor_decode.h>
#include <mgmt/mcumgr/util/zcbor_bulk.h>
#include <smp_internal.h>

#define SMP_RESPONSE_WAIT_TIME   3
#define ZCBOR_HISTORY_ARRAY_SIZE 4

/* Test os_mgmt echo command with 40 bytes of data: "short MCUMGR test application message..." */
static const uint8_t command[] = {
	0x02, 0x00, 0x00, 0x2e, 0x00, 0x00, 0x01, 0x00, 0xbf, 0x61, 0x64, 0x78, 0x28, 0x73,
	0x68, 0x6f, 0x72, 0x74, 0x20, 0x4d, 0x43, 0x55, 0x4d, 0x47, 0x52, 0x20, 0x74, 0x65,
	0x73, 0x74, 0x20, 0x61, 0x70, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e,
	0x20, 0x6d, 0x65, 0x73, 0x73, 0x61, 0x67, 0x65, 0x2e, 0x2e, 0x2e, 0xff,
};

/* "d" field switched to "e" */
static const uint8_t command_invalid[] = {
	0x02, 0x00, 0x00, 0x2e, 0x00, 0x00, 0x01, 0x00, 0xbf, 0x61, 0x65, 0x78, 0x28, 0x73,
	0x68, 0x6f, 0x72, 0x74, 0x20, 0x4d, 0x43, 0x55, 0x4d, 0x47, 0x52, 0x20, 0x74, 0x65,
	0x73, 0x74, 0x20, 0x61, 0x70, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e,
	0x20, 0x6d, 0x65, 0x73, 0x73, 0x61, 0x67, 0x65, 0x2e, 0x2e, 0x2e, 0xff,
};

/* Expected response from mcumgr */
static const uint8_t expected_response[] = {
	0x03, 0x00, 0x00, 0x2e, 0x00, 0x00, 0x01, 0x00, 0xbf, 0x61, 0x72, 0x78, 0x28, 0x73,
	0x68, 0x6f, 0x72, 0x74, 0x20, 0x4d, 0x43, 0x55, 0x4d, 0x47, 0x52, 0x20, 0x74, 0x65,
	0x73, 0x74, 0x20, 0x61, 0x70, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e,
	0x20, 0x6d, 0x65, 0x73, 0x73, 0x61, 0x67, 0x65, 0x2e, 0x2e, 0x2e, 0xff};

ZTEST(os_mgmt_echo, test_echo)
{
	struct net_buf *nb;

	/* Enable dummy SMP backend and ready for usage */
	smp_dummy_enable();
	smp_dummy_clear_state();

	/* Send test echo command to dummy SMP backend */
	(void)smp_dummy_tx_pkt(command, sizeof(command));
	smp_dummy_add_data();

	/* For a short duration to see if response has been received */
	bool received = smp_dummy_wait_for_data(SMP_RESPONSE_WAIT_TIME);

	zassert_true(received, "Expected to receive data but timed out\n");

	/* Retrieve response buffer and ensure validity */
	nb = smp_dummy_get_outgoing();
	smp_dummy_disable();

	zassert_equal(sizeof(expected_response), nb->len,
		      "Expected to receive %d bytes but got %d\n", sizeof(expected_response),
		      nb->len);

	zassert_mem_equal(expected_response, nb->data, nb->len, "Expected received data mismatch");
}

ZTEST(os_mgmt_echo, test_echo_invalid)
{
	zcbor_state_t zsd[ZCBOR_HISTORY_ARRAY_SIZE] = {0};
	struct net_buf *nb;
	size_t decoded = 0;
	bool ok;
	int rc;

	struct zcbor_map_decode_key_val output_decode[] = {
		ZCBOR_MAP_DECODE_KEY_DECODER("rc", zcbor_int32_decode, &rc),
	};

	/* Enable dummy SMP backend and ready for usage */
	smp_dummy_enable();
	smp_dummy_clear_state();

	/* Send test echo command to dummy SMP backend */
	(void)smp_dummy_tx_pkt(command_invalid, sizeof(command_invalid));
	smp_dummy_add_data();

	/* For a short duration to see if response has been received */
	bool received = smp_dummy_wait_for_data(SMP_RESPONSE_WAIT_TIME);

	zassert_true(received, "Expected to receive data but timed out\n");

	/* Retrieve response buffer and ensure error detected */
	nb = smp_dummy_get_outgoing();
	smp_dummy_disable();

	/* Process received data by removing header */
	(void)net_buf_pull(nb, sizeof(struct smp_hdr));
	zcbor_new_decode_state(zsd, 4, nb->data, nb->len, 1, NULL, 0);

	/* Expect error result */
	ok = zcbor_map_decode_bulk(zsd, output_decode, ARRAY_SIZE(output_decode), &decoded) == 0;
	zassert_true(ok, "Expected decode to be successful");
	zassert_equal(decoded, 1, "Expected to receive one decoded element");
	zassert_true(
		zcbor_map_decode_bulk_key_found(output_decode, ARRAY_SIZE(output_decode), "rc"),
		"Expected to receive rc element");
	zassert_equal(MGMT_ERR_EINVAL, rc, "Expected invalid command to be detected");
}

ZTEST_SUITE(os_mgmt_echo, NULL, NULL, NULL, NULL, NULL);
