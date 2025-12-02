/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 * Copyright (c) 2023 Jamie M.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/net_buf.h>
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

#include <infuse/common_boot.h>
#include <infuse/time/epoch.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

#define SMP_RESPONSE_WAIT_TIME   3
#define ZCBOR_BUFFER_SIZE        256
#define OUTPUT_BUFFER_SIZE       256
#define ZCBOR_HISTORY_ARRAY_SIZE 4

static struct net_buf *nb;
static void cleanup_test(void *p);

static void send_reset(uint8_t expected_result)
{
	uint8_t buffer[ZCBOR_BUFFER_SIZE];
	uint8_t buffer_out[OUTPUT_BUFFER_SIZE];
	zcbor_state_t zse[ZCBOR_HISTORY_ARRAY_SIZE] = {0};
	zcbor_state_t zsd[ZCBOR_HISTORY_ARRAY_SIZE] = {0};
	uint16_t buffer_size;
	size_t decoded = 0;
	bool received, ok;
	int rc;

	struct zcbor_map_decode_key_val output_decode[] = {
		ZCBOR_MAP_DECODE_KEY_DECODER("rc", zcbor_int32_decode, &rc),
	};

	memset(buffer, 0, sizeof(buffer));
	memset(buffer_out, 0, sizeof(buffer_out));
	buffer_size = 0;
	memset(zse, 0, sizeof(zse));
	memset(zsd, 0, sizeof(zsd));

	/* Send reset command */
	zcbor_new_encode_state(zse, 2, buffer, ARRAY_SIZE(buffer), 0);
	ok = create_mcumgr_reset_packet(zse, false, buffer, buffer_out, &buffer_size);
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
	zassert_not_null(nb);
	smp_dummy_disable();

	/* Process received data by removing header */
	(void)net_buf_pull(nb, sizeof(struct smp_hdr));
	zcbor_new_decode_state(zsd, 4, nb->data, nb->len, 1, NULL, 0);

	ok = zcbor_map_decode_bulk(zsd, output_decode, ARRAY_SIZE(output_decode), &decoded) == 0;
	zassert_true(ok, "Expected decode to be successful");
	if (expected_result == MGMT_ERR_EOK) {
		zassert_equal(decoded, 0, "Did not expect any decoded elements");
		zassert_false(zcbor_map_decode_bulk_key_found(output_decode,
							      ARRAY_SIZE(output_decode), "rc"),
			      "Did not expect to receive rc element");
	} else {
		zassert_equal(decoded, 1, "Expected to receive one decoded element");
		zassert_true(zcbor_map_decode_bulk_key_found(output_decode,
							     ARRAY_SIZE(output_decode), "rc"),
			     "Expected to receive rc element");
		zassert_equal(expected_result, rc);
	}
	net_buf_unref(nb);
	nb = NULL;
}

ZTEST(os_mgmt_reset, test_reset)
{
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots;
	__maybe_unused struct infuse_reboot_state reboot_state;
	ssize_t rc;

	/* KV store should have been initialised and populated with a reboot count */
	rc = KV_STORE_READ(KV_KEY_REBOOTS, &reboots);
	zassert_equal(sizeof(reboots), rc);

	switch (reboots.count) {
#ifdef CONFIG_INFUSE_REBOOT
	case 1:
#if CONFIG_MCUMGR_GRP_OS_INFUSE_RESET_MIN_UPTIME > 0
		/* Send reset command on boot, should fail */
		send_reset(MGMT_ERR_EBUSY);
		/* Command should still fail  */
		k_sleep(K_TIMEOUT_ABS_SEC(CONFIG_MCUMGR_GRP_OS_INFUSE_RESET_MIN_UPTIME - 1));
		send_reset(MGMT_ERR_EBUSY);
		/* Wait until command should work */
		k_sleep(K_TIMEOUT_ABS_SEC(CONFIG_MCUMGR_GRP_OS_INFUSE_RESET_MIN_UPTIME));
#endif /* CONFIG_MCUMGR_GRP_OS_INFUSE_RESET_MIN_UPTIME > 0 */
		/* Send reset command */
		send_reset(MGMT_ERR_EOK);
		/* Wait for the reboot */
		k_sleep(K_SECONDS(3));
		zassert_unreachable("Reset command did not trigger reboot");
		break;
	case 2:
		/* Validate previous reboot information */
		rc = infuse_common_boot_last_reboot(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal(INFUSE_REBOOT_MCUMGR, reboot_state.reason);
		zassert_equal(0, reboot_state.info.generic.info1);
		zassert_equal(0, reboot_state.info.generic.info2);
		break;
#else
	case 1:
		/* Send reset command on boot, should fail */
		send_reset(MGMT_ERR_ENOTSUP);
		break;
#endif /* !CONFIG_INFUSE_REBOOT */
	default:
		zassert_unreachable("Unexpected reboot count");
		break;
	}
}

static void cleanup_test(void *p)
{
	if (nb != NULL) {
		net_buf_unref(nb);
		nb = NULL;
	}
}

/* Time not set test set */
ZTEST_SUITE(os_mgmt_reset, NULL, NULL, NULL, cleanup_test, NULL);
