/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/random/random.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/sys/crc.h>

#include <infuse/dfu/helpers.h>
#include <infuse/security.h>
#include <infuse/common_boot.h>
#include <infuse/types.h>
#include <infuse/rpc/types.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/time/epoch.h>

struct rpc_coap_download_request_send {
	struct rpc_coap_download_request core;
	char resource[128];
} __packed;

struct rpc_coap_download_request_v2_send {
	struct rpc_coap_download_v2_request core;
	char resource[128];
} __packed;

#ifdef CONFIG_TEST_NATIVE_MOCK
static size_t bt_image_len;
static size_t bt_image_crc;
static uint32_t bt_fail_after;
static bool bt_in_progress;
static int bt_start_rc;
static int bt_finish_rc;

int bt_controller_manager_file_write_start(uint32_t *ctx, uint8_t action, size_t image_len)
{
	bt_image_len = image_len;
	bt_image_crc = 0;
	if (bt_start_rc == 0) {
		bt_in_progress = true;
	}
	return bt_start_rc;
}

int bt_controller_manager_file_write_next(uint32_t ctx, uint32_t image_offset,
					  const void *image_chunk, size_t chunk_len)
{
	zassert_equal(0, image_offset % sizeof(uint32_t));

	bt_image_crc = crc32_ieee_update(bt_image_crc, image_chunk, chunk_len);

	if (bt_fail_after > 0) {
		if (--bt_fail_after == 0) {
			return -EIO;
		}
	}
	return 0;
}

int bt_controller_manager_file_write_finish(uint32_t ctx, uint32_t *len, uint32_t *crc)
{
	*len = bt_image_len;
	*crc = bt_image_crc;
	bt_in_progress = false;
	return bt_finish_rc;
}

#endif /* CONFIG_TEST_NATIVE_MOCK */

static void send_download_command(uint32_t request_id, const char *server, uint16_t port,
				  uint16_t timeout, uint8_t action, char *resource, uint32_t len,
				  uint32_t crc)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_coap_download_request_send params = {
		.core = {
			.header =
				{
					.request_id = request_id,
					.command_id = RPC_ID_COAP_DOWNLOAD,
				},
			.server_port = port,
			.block_timeout_ms = timeout,
			.action = action,
			.resource_crc = crc,
			.resource_len = len,
		}};

	strncpy(params.core.server_address, server, sizeof(params.core.server_address));
	params.core.server_port = port;
	strncpy(params.resource, resource, sizeof(params.resource));

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static void send_download_v2_command(uint32_t request_id, const char *server, uint16_t port,
				     uint16_t timeout, uint8_t action, char *resource, uint32_t len,
				     uint32_t crc, uint16_t block_size)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_coap_download_request_v2_send params = {
		.core = {
			.header =
				{
					.request_id = request_id,
					.command_id = RPC_ID_COAP_DOWNLOAD_V2,
				},
			.server_port = port,
			.block_timeout_ms = timeout,
			.block_size = block_size,
			.action = action,
			.resource_crc = crc,
			.resource_len = len,
		}};

	strncpy(params.core.server_address, server, sizeof(params.core.server_address));
	params.core.server_port = port;
	strncpy(params.resource, resource, sizeof(params.resource));

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static void expect_coap_download_response(uint32_t request_id, int16_t rc, uint32_t len,
					  uint32_t crc)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_coap_download_response *response;
	struct net_buf *rsp;

	zassert_not_null(response_queue);

	/* Response was sent */
	rsp = k_fifo_get(response_queue, K_SECONDS(30));
	zassert_not_null(rsp);
	response = (void *)(rsp->data + sizeof(struct epacket_dummy_frame));

	/* Parameters match what we expect */
	zassert_equal(request_id, response->header.request_id);
	zassert_equal(rc, response->header.return_code);
	zassert_equal(len, response->resource_len);
	zassert_equal(crc, response->resource_crc);

	/* Free the response */
	net_buf_unref(rsp);
}

ZTEST(rpc_command_coap_download, test_download_invalid)
{
	/* Bad actions */
	send_download_command(1, "coap.dev.infuse-iot.com", 5684, 0, 5, "file/small", UINT32_MAX,
			      UINT32_MAX);
	expect_coap_download_response(1, -EINVAL, 0, 0);
	send_download_command(2, "coap.dev.infuse-iot.com", 5684, 0, 5, "file/small", UINT32_MAX,
			      UINT32_MAX);
	expect_coap_download_response(2, -EINVAL, 0, 0);

	/* Bad file */
	send_download_command(5, "coap.dev.infuse-iot.com", 5684, 0, RPC_ENUM_FILE_ACTION_DISCARD,
			      "file/doesn't-exist", UINT32_MAX, UINT32_MAX);
	expect_coap_download_response(5, -404, 0, 0);

	/* Bad server */
	send_download_command(3, "coap.dev.infuse-iot-none.com", 5684, 0,
			      RPC_ENUM_FILE_ACTION_DISCARD, "file/small", UINT32_MAX, UINT32_MAX);
	expect_coap_download_response(3, -ENOENT, 0, 0);

	/* Bad port */
	send_download_command(4, "coap.dev.infuse-iot.com", 1000, 0, RPC_ENUM_FILE_ACTION_DISCARD,
			      "file/small", UINT32_MAX, UINT32_MAX);
	expect_coap_download_response(4, -ETIMEDOUT, 0, 0);

	/* Connect failure */
	send_download_command(6, "www.google.com", 5684, 0, RPC_ENUM_FILE_ACTION_DISCARD,
			      "file/small", UINT32_MAX, UINT32_MAX);
	expect_coap_download_response(6, -ETIMEDOUT, 0, 0);

	/* Tiny timeout */
	send_download_command(20, "coap.dev.infuse-iot.com", 5684, 1, RPC_ENUM_FILE_ACTION_DISCARD,
			      "file/small_file", UINT32_MAX, UINT32_MAX);
	expect_coap_download_response(20, -ETIMEDOUT, 0, 0);

#ifdef CONFIG_TLS_CREDENTIALS
	sec_tag_t tag = infuse_security_coap_dtls_tag();
	char cred[16];
	size_t cred_len;

	/* Cache the credential */
	cred_len = sizeof(cred);
	zassert_equal(0, tls_credential_get(tag, TLS_CREDENTIAL_PSK_ID, cred, &cred_len));
	zassert_equal(sizeof(cred), cred_len);

	/* Delete the credential */
	zassert_equal(0, tls_credential_delete(tag, TLS_CREDENTIAL_PSK_ID));

	/* Basic discard download */
	send_download_command(100, "coap.dev.infuse-iot.com", 5684, 0, RPC_ENUM_FILE_ACTION_DISCARD,
			      "file/small_file", UINT32_MAX, UINT32_MAX);
	expect_coap_download_response(100, -EINVAL, 0, 0);

	/* Re-add the credential */
	zassert_equal(0, tls_credential_add(tag, TLS_CREDENTIAL_PSK_ID, cred, sizeof(cred)));
#endif /* CONFIG_TLS_CREDENTIALS */

	/* Everything works after all the failures */
	send_download_command(10, "coap.dev.infuse-iot.com", 5684, 0, RPC_ENUM_FILE_ACTION_DISCARD,
			      "file/small_file", UINT32_MAX, UINT32_MAX);
	expect_coap_download_response(10, 0, 12, 0xb5289bef);
}

ZTEST(rpc_command_coap_download, test_download)
{
	const struct flash_area *fa;

	/* Ensure consistent starting point */
	flash_area_open(FIXED_PARTITION_ID(slot1_partition), &fa);
	flash_area_erase(fa, 0, fa->fa_size);
	flash_area_close(fa);

	/* Basic discard download */
	send_download_command(10, "coap.dev.infuse-iot.com", 5684, 0, RPC_ENUM_FILE_ACTION_DISCARD,
			      "file/small_file", UINT32_MAX, UINT32_MAX);
	expect_coap_download_response(10, 0, 12, 0xb5289bef);

	/* Larger discard download */
	send_download_command(10, "coap.dev.infuse-iot.com", 5684, 0, RPC_ENUM_FILE_ACTION_DISCARD,
			      "file/med_file", UINT32_MAX, UINT32_MAX);
	expect_coap_download_response(10, 0, 10030, 0x9919d24e);

	/* Small DFU download of unknown length and size */
	send_download_command(20, "coap.dev.infuse-iot.com", 5684, 0, RPC_ENUM_FILE_ACTION_APP_IMG,
			      "file/small_file", UINT32_MAX, UINT32_MAX);
	expect_coap_download_response(20, 0, 12, 0xb5289bef);

	/* Download file contents for DFU */
	send_download_command(10, "coap.dev.infuse-iot.com", 5684, 0, RPC_ENUM_FILE_ACTION_APP_IMG,
			      "file/med_file", 10030, 0x9919d24e);
	expect_coap_download_response(10, 0, 10030, 0x9919d24e);

	/* Second time should detect file has already been downloaded */
	send_download_command(11, "coap.dev.infuse-iot.com", 5684, 0, RPC_ENUM_FILE_ACTION_APP_IMG,
			      "file/med_file", 10030, 0x9919d24e);
	expect_coap_download_response(11, 0, 0, 0x9919d24e);

	/* But if CRC not provided, file is downloaded again */
	send_download_command(12, "coap.dev.infuse-iot.com", 5684, 0, RPC_ENUM_FILE_ACTION_APP_IMG,
			      "file/med_file", 10030, UINT32_MAX);
	expect_coap_download_response(12, 0, 10030, 0x9919d24e);

	/* DFU requested too large */
	send_download_command(20, "coap.dev.infuse-iot.com", 5684, 0, RPC_ENUM_FILE_ACTION_APP_IMG,
			      "file/small_file", UINT32_MAX / 2, UINT32_MAX);
	expect_coap_download_response(20, -EINVAL, 0, 0);

	/* Balanced call count */
	zassert_equal(0, infuse_dfu_write_erase_call_count());

	/* V2 RPCs test
	 * Should be its own subtest, but TLS credentials causing weird issues after
	 * the TLS remove/add test in test_invalid
	 */
	/* Basic discard download */
	send_download_v2_command(48, "coap.dev.infuse-iot.com", 5684, 0,
				 RPC_ENUM_FILE_ACTION_DISCARD, "file/small_file", UINT32_MAX,
				 UINT32_MAX, 0);
	expect_coap_download_response(48, 0, 12, 0xb5289bef);

	/* Larger discard download */
	send_download_v2_command(50, "coap.dev.infuse-iot.com", 5684, 0,
				 RPC_ENUM_FILE_ACTION_DISCARD, "file/med_file", UINT32_MAX,
				 UINT32_MAX, 1024);
	expect_coap_download_response(50, 0, 10030, 0x9919d24e);
}

ZTEST(rpc_command_coap_download, test_download_bt_ctlr)
{
#ifdef CONFIG_TEST_NATIVE_MOCK
	send_download_command(15, "coap.dev.infuse-iot.com", 5684, 0,
			      RPC_ENUM_FILE_ACTION_BT_CTLR_IMG, "file/med_file", 10030, UINT32_MAX);
	expect_coap_download_response(15, 0, 10030, 0x9919d24e);
	zassert_false(bt_in_progress);

	bt_start_rc = -EIO;
	send_download_command(20, "coap.dev.infuse-iot.com", 5684, 0,
			      RPC_ENUM_FILE_ACTION_BT_CTLR_IMG, "file/med_file", 10030, UINT32_MAX);
	expect_coap_download_response(20, -EIO, 0, 0);
	zassert_false(bt_in_progress);
	bt_start_rc = 0;

	bt_fail_after = 10;
	send_download_command(16, "coap.dev.infuse-iot.com", 5684, 0,
			      RPC_ENUM_FILE_ACTION_BT_CTLR_IMG, "file/med_file", 10030, UINT32_MAX);
	expect_coap_download_response(16, -EIO, 0, 0);
	zassert_false(bt_in_progress);

	bt_finish_rc = -EINVAL;
	send_download_command(30, "coap.dev.infuse-iot.com", 5684, 0,
			      RPC_ENUM_FILE_ACTION_BT_CTLR_IMG, "file/med_file", 10030, UINT32_MAX);
	expect_coap_download_response(30, -EINVAL, 10030, 0x9919d24e);
	zassert_false(bt_in_progress);
	bt_finish_rc = 0;

	send_download_command(17, "coap.dev.infuse-iot.com", 5684, 0,
			      RPC_ENUM_FILE_ACTION_BT_CTLR_IMG, "file/med_file", 10030, UINT32_MAX);
	expect_coap_download_response(17, 0, 10030, 0x9919d24e);
	zassert_false(bt_in_progress);
	zassert_false(bt_in_progress);
#endif /* CONFIG_TEST_NATIVE_MOCK */
}

static void flash_area_copy_wrapped(uint8_t partition_dst, uint8_t partition_src, uint32_t len)
{
	const struct flash_area *fa_dst, *fa_src;
	uint8_t buffer[128];

	zassert_equal(0, flash_area_open(partition_dst, &fa_dst));
	zassert_equal(0, flash_area_open(partition_src, &fa_src));

	zassert_equal(0, flash_area_erase(fa_dst, 0, fa_dst->fa_size));
	zassert_equal(
		0, flash_area_copy(fa_src, 0, fa_dst, 0, fa_dst->fa_size, buffer, sizeof(buffer)));

	flash_area_close(fa_dst);
	flash_area_close(fa_src);
}

ZTEST(rpc_command_coap_download, test_download_cpatch)
{
	const struct flash_area *fa_dst;

	/* Clear any previous state in the original image slot */
	zassert_equal(0, flash_area_open(FIXED_PARTITION_ID(slot0_partition), &fa_dst));
	zassert_equal(0, flash_area_erase(fa_dst, 0, fa_dst->fa_size));
	flash_area_close(fa_dst);

	/* Attempting to run the patch file initially should fail due to invalid original data */
	send_download_command(20, "coap.dev.infuse-iot.com", 5684, 0,
			      RPC_ENUM_FILE_ACTION_APP_CPATCH, "file/hello_world-validate", 333,
			      UINT32_MAX);
	expect_coap_download_response(20, -EINVAL, 333, 0x8451810D);

	/* Download the base image into partition1 */
	send_download_command(21, "coap.dev.infuse-iot.com", 5684, 0, RPC_ENUM_FILE_ACTION_APP_IMG,
			      "file/hello_world", 18940, UINT32_MAX);
	expect_coap_download_response(21, 0, 18940, 0xE58FF061);

	/* Copy the base image into partition0 */
	flash_area_copy_wrapped(FIXED_PARTITION_ID(slot0_partition),
				FIXED_PARTITION_ID(slot1_partition), 18940);

	/* Patch file should download and apply cleanly now */
	send_download_command(22, "coap.dev.infuse-iot.com", 5684, 0,
			      RPC_ENUM_FILE_ACTION_APP_CPATCH, "file/hello_world-validate", 333,
			      UINT32_MAX);
	expect_coap_download_response(22, 0, 333, 0x8451810D);

	/* Balanced call count */
	zassert_equal(0, infuse_dfu_write_erase_call_count());
}

ZTEST_SUITE(rpc_command_coap_download, NULL, NULL, NULL, NULL, NULL);
