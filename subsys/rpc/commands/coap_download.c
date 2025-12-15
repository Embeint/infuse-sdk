/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net_buf.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/dfu/mcuboot.h>

#include <infuse/dfu/helpers.h>
#include <infuse/rpc/commands.h>
#include <infuse/rpc/types.h>
#include <infuse/fs/kv_store.h>
#include <infuse/security.h>
#include <infuse/net/coap.h>
#include <infuse/net/dns.h>
#include <infuse/epacket/packet.h>
#include <infuse/reboot.h>

#ifdef CONFIG_NRF_MODEM_LIB
#include "nrf_modem_delta_dfu.h"
#endif /* CONFIG_NRF_MODEM_LIB */

#include "common_file_actions.h"

LOG_MODULE_DECLARE(rpc_server, CONFIG_INFUSE_RPC_LOG_LEVEL);

static int data_cb(uint32_t offset, const uint8_t *data, uint16_t data_len, void *context)
{
	struct rpc_common_file_actions_ctx *ctx = context;
	int rc;

	/* Prevent RPC server watchdog channel timing out */
	rpc_server_watchdog_feed();

	rc = rpc_common_file_actions_write(ctx, offset, data, data_len);
	if (rc < 0) {
		LOG_ERR("Failed to handle offset %08X (%d)", offset, rc);
	}
	return rc;
}

int rpc_command_coap_download_run(struct rpc_coap_download_v2_request *req, char *resource,
				  struct rpc_coap_download_v2_response *rsp, int *downloaded)
{
	const sec_tag_t sec_tls_tags[] = {
		infuse_security_coap_dtls_tag(),
	};
	struct rpc_common_file_actions_ctx ctx;
	int block_timeout = req->block_timeout_ms == 0 ? 1000 : req->block_timeout_ms;
	struct sockaddr address;
	socklen_t address_len;
	uint8_t *work_mem;
	size_t work_mem_size;
	int sock = -1;
	int rc;

	work_mem = rpc_server_command_working_mem(&work_mem_size);

	*downloaded = 0;
	rc = rpc_common_file_actions_start(&ctx, req->action, req->resource_len, req->resource_crc);
	if (rc == FILE_ALREADY_PRESENT) {
		LOG_INF("File already present");
		goto download_done;
	}
	if (rc < 0) {
		LOG_ERR("Failed to prepare for %d (%d)", req->action, rc);
		goto error;
	}

	/* Preparing may have taken a while */
	rpc_server_watchdog_feed();

	/* DNS query on provided address */
	rc = infuse_sync_dns(req->server_address, req->server_port, AF_INET, SOCK_DGRAM, &address,
			     &address_len);
	if (rc < 0) {
		LOG_DBG("DNS failure (%d)", rc);
		goto error;
	}

	/* Create socket */
	sock = zsock_socket(address.sa_family, SOCK_DGRAM, IPPROTO_DTLS_1_2);
	if (sock < 0) {
		LOG_DBG("zsock_socket failure (%d)", -errno);
		rc = -errno;
		goto error;
	}

	/* Assign DTLS security tags */
	if (zsock_setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST, sec_tls_tags, sizeof(sec_tls_tags)) <
	    0) {
		LOG_DBG("zsock_setsockopt failure (%d)", -errno);
		rc = -errno;
		goto error;
	}

#ifndef CONFIG_NRF_MODEM_LIB
	int timeout = 2500;

	/* Reduce timeout from the default 1 minute */
	if (zsock_setsockopt(sock, SOL_TLS, TLS_DTLS_HANDSHAKE_TIMEOUT_MAX, &timeout,
			     sizeof(timeout)) < 0) {
		LOG_DBG("zsock_setsockopt failure (%d)", -errno);
		rc = -errno;
		goto error;
	}
#endif /* CONFIG_NRF_MODEM_LIB */

	/* Complete DTLS handshake */
	rc = zsock_connect(sock, &address, address_len);
	if (rc != 0) {
		LOG_DBG("zsock_connect failure (%d)", -errno);
		rc = -errno;
		goto error;
	}

	/* Download the resource */
	*downloaded = infuse_coap_download(sock, resource, data_cb, &ctx, work_mem, work_mem_size,
					   req->block_size, block_timeout);
	if (*downloaded < 0) {
		rc = *downloaded;
		*downloaded = 0;
		LOG_DBG("infuse_coap_download failed (%d)", rc);
		goto error;
	}

	/* Close the socket */
	zsock_close(sock);

download_done:
	/* Finish file write process */
	rc = rpc_common_file_actions_finish(&ctx, RPC_ID_COAP_DOWNLOAD, false);
	if (rc < 0) {
		LOG_ERR("Failed to finish %d (%d)", req->action, rc);
	}
	rsp->resource_len = ctx.received;
	rsp->resource_crc = ctx.crc;
	return rc;

error:
	/* Cleanup resources */
	(void)rpc_common_file_actions_error_cleanup(&ctx);

	/* Close socket if open */
	if (sock >= 0) {
		zsock_close(sock);
	}

	rsp->resource_len = 0;
	rsp->resource_crc = 0;
	return rc;
}

/* Responses are equivalent */
BUILD_ASSERT(sizeof(struct rpc_coap_download_response) ==
	     sizeof(struct rpc_coap_download_v2_response));

#ifdef CONFIG_INFUSE_RPC_COMMAND_COAP_DOWNLOAD
struct net_buf *rpc_command_coap_download(struct net_buf *request)
{
	struct rpc_coap_download_request *req = (void *)request->data;
	struct rpc_coap_download_v2_response rsp = {0};
	int downloaded;
	int rc;

	/* Copy legacy request over to new format (with auto block size) */
	struct rpc_coap_download_v2_request req_v2 = {
		.header = req->header,
		.server_port = req->server_port,
		.block_timeout_ms = req->block_timeout_ms,
		.block_size = 0,
		.action = req->action,
		.resource_len = req->resource_len,
		.resource_crc = req->resource_crc,
	};

	memcpy(req_v2.server_address, req->server_address, sizeof(req_v2.server_address));

	/* Run the command */
	rc = rpc_command_coap_download_run(&req_v2, req->resource, &rsp, &downloaded);

	/* Return the response */
	return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
}
#endif /* CONFIG_INFUSE_RPC_COMMAND_COAP_DOWNLOAD */

#ifdef CONFIG_INFUSE_RPC_COMMAND_COAP_DOWNLOAD_V2
struct net_buf *rpc_command_coap_download_v2(struct net_buf *request)
{
	struct rpc_coap_download_v2_request *req = (void *)request->data;
	struct rpc_coap_download_v2_response rsp = {0};
	int downloaded;
	int rc;

	/* Run the command */
	rc = rpc_command_coap_download_run(req, req->resource, &rsp, &downloaded);

	/* Return the response */
	return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
}
#endif /* CONFIG_INFUSE_RPC_COMMAND_COAP_DOWNLOAD_V2 */
