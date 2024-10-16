/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/random/random.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/net/tls_credentials.h>

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

static void expect_coap_download_response(uint32_t request_id, int16_t rc, uint32_t len,
					  uint32_t crc)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_coap_download_response *response;
	struct net_buf *rsp;

	zassert_not_null(response_queue);

	/* Response was sent */
	rsp = net_buf_get(response_queue, K_SECONDS(30));
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
	send_download_command(5, "coap.dev.infuse-iot.com", 5684, 0, 0, "file/doesn't-exist",
			      UINT32_MAX, UINT32_MAX);
	expect_coap_download_response(5, -404, 0, 0);

	/* Bad server */
	send_download_command(3, "coap.dev.infuse-iot-none.com", 5684, 0, 0, "file/small",
			      UINT32_MAX, UINT32_MAX);
	expect_coap_download_response(3, -ENOENT, 0, 0);

	/* Bad port */
	send_download_command(4, "coap.dev.infuse-iot.com", 1000, 0, 0, "file/small", UINT32_MAX,
			      UINT32_MAX);
	expect_coap_download_response(4, -ETIMEDOUT, 0, 0);

	/* Connect failure */
	send_download_command(6, "www.google.com", 5684, 0, 0, "file/small", UINT32_MAX,
			      UINT32_MAX);
	expect_coap_download_response(6, -ETIMEDOUT, 0, 0);

	/* Tiny timeout */
	send_download_command(20, "coap.dev.infuse-iot.com", 5684, 1, 0, "file/small_file",
			      UINT32_MAX, UINT32_MAX);
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
	send_download_command(100, "coap.dev.infuse-iot.com", 5684, 0, 0, "file/small_file",
			      UINT32_MAX, UINT32_MAX);
	expect_coap_download_response(100, -ENOENT, 0, 0);

	/* Re-add the credential */
	zassert_equal(0, tls_credential_add(tag, TLS_CREDENTIAL_PSK_ID, cred, sizeof(cred)));
#endif /* CONFIG_TLS_CREDENTIALS */
}

ZTEST(rpc_command_coap_download, test_download)
{
	const struct flash_area *fa;

	/* Ensure consistent starting point */
	flash_area_open(FIXED_PARTITION_ID(slot1_partition), &fa);
	flash_area_erase(fa, 0, fa->fa_size);
	flash_area_close(fa);

	/* Basic discard download */
	send_download_command(10, "coap.dev.infuse-iot.com", 5684, 0, 0, "file/small_file",
			      UINT32_MAX, UINT32_MAX);
	expect_coap_download_response(10, 0, 12, 0xb5289bef);

	/* Larger discard download */
	send_download_command(10, "coap.dev.infuse-iot.com", 5684, 0, 0, "file/med_file",
			      UINT32_MAX, UINT32_MAX);
	expect_coap_download_response(10, 0, 10030, 0x9919d24e);

	/* Download file contents for DFU */
	send_download_command(10, "coap.dev.infuse-iot.com", 5684, 0, 1, "file/med_file", 10030,
			      0x9919d24e);
	expect_coap_download_response(10, 0, 10030, 0x9919d24e);

	/* Second time should detect file has already been downloaded */
	send_download_command(11, "coap.dev.infuse-iot.com", 5684, 0, 1, "file/med_file", 10030,
			      0x9919d24e);
	expect_coap_download_response(11, 0, 0, 0x9919d24e);

	/* But if CRC not provided, file is downloaded again */
	send_download_command(12, "coap.dev.infuse-iot.com", 5684, 0, 1, "file/med_file", 10030,
			      UINT32_MAX);
	expect_coap_download_response(12, 0, 10030, 0x9919d24e);
}

ZTEST_SUITE(rpc_command_coap_download, NULL, NULL, NULL, NULL, NULL);
