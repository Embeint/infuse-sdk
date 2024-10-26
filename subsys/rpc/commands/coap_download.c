/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/net/buf.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/dfu/mcuboot.h>

#include <infuse/dfu/helpers.h>
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

#include "../server.h"

LOG_MODULE_DECLARE(rpc_server);

struct cb_ctx {
	const struct flash_area *fa;
	uint32_t expected_len;
	uint32_t crc;
	uint8_t action;
};

static int data_cb(uint32_t offset, const uint8_t *data, uint16_t data_len, void *context)
{
	struct cb_ctx *ctx = context;
	int rc;

	ARG_UNUSED(rc);

	/* Prevent RPC server watchdog channel timing out */
	rpc_server_watchdog_feed();

	/* Update CRC (Initialised to 0 in main function) */
	ctx->crc = crc32_ieee_update(ctx->crc, data, data_len);

	switch (ctx->action) {
#ifdef CONFIG_INFUSE_DFU_HELPERS
	case RPC_ENUM_FILE_ACTION_APP_IMG:
		/* Erase receiving area on first packet */
		if (offset == 0) {
			rc = infuse_dfu_image_erase(ctx->fa, ctx->expected_len);
			if (rc < 0) {
				LOG_ERR("DFU: Failed to erase (%d)", rc);
				return rc;
			}
		}
		/* Write to opened flash area */
		rc = flash_area_write(ctx->fa, offset, data, data_len);
		if (rc < 0) {
			LOG_ERR("DFU: Failed to write at 0x%08x (%d)", offset, rc);
			return rc;
		}
		break;
#endif /* CONFIG_INFUSE_DFU_HELPERS */
#ifdef CONFIG_NRF_MODEM_LIB
	case RPC_ENUM_FILE_ACTION_NRF91_MODEM_DIFF:
		rc = nrf_modem_delta_dfu_write(data, data_len);
		if (rc != 0) {
			LOG_ERR("Modem: Failed to write at 0x%08x (%d)", offset, rc);
			return rc;
		}
		break;
#endif /* CONFIG_NRF_MODEM_LIB */
	}
	return 0;
}

struct net_buf *rpc_command_coap_download(struct net_buf *request)
{
	struct rpc_coap_download_request *req = (void *)request->data;
	const sec_tag_t sec_tls_tags[] = {
		infuse_security_coap_dtls_tag(),
	};
	struct cb_ctx context = {
		.action = req->action,
		.expected_len = req->resource_len,
		.crc = 0,
	};
	int block_timeout = req->block_timeout_ms == 0 ? 1000 : req->block_timeout_ms;
	struct sockaddr address;
	socklen_t address_len;
	uint8_t *work_mem;
	size_t work_mem_size;
	uint32_t crc;
	int sock = -1;
	int downloaded = 0;
	int rc;

	work_mem = rpc_server_command_working_mem(&work_mem_size);

	/* Validate requested action */
	switch (req->action) {
	case RPC_ENUM_FILE_ACTION_DISCARD:
		break;
#if FIXED_PARTITION_EXISTS(slot1_partition)
	case RPC_ENUM_FILE_ACTION_APP_IMG:
		rc = flash_area_open(FIXED_PARTITION_ID(slot1_partition), &context.fa);
		if (rc < 0) {
			goto error;
		}
		if (req->resource_crc != UINT32_MAX) {
			rc = flash_area_crc32(context.fa, 0, req->resource_len, &crc, work_mem,
					      work_mem_size);
			if (rc < 0) {
				goto error;
			}
			if (req->resource_crc == crc) {
				LOG_INF("Resource CRC already matches");
				context.crc = crc;
				rc = 0;
				goto done;
			}
		}
		break;
#endif /* FIXED_PARTITION_EXISTS(slot1_partition) */
#ifdef CONFIG_NRF_MODEM_LIB
	case RPC_ENUM_FILE_ACTION_NRF91_MODEM_DIFF:
		rc = infuse_dfu_nrf91_modem_delta_prepare();
		if (rc > 0) {
			rc = -EIO;
		}
		if (rc < 0) {
			LOG_ERR("Failed to prepare modem (%d)", rc);
			goto error;
		}
		break;
#endif /* CONFIG_NRF_MODEM_LIB */
	default:
		rc = -EINVAL;
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
	downloaded = infuse_coap_download(sock, req->resource, data_cb, &context, work_mem,
					  work_mem_size, block_timeout);
	if (downloaded < 0) {
		LOG_DBG("infuse_coap_download failed (%d)", downloaded);
		rc = downloaded;
		goto error;
	}

	/* Close the socket */
	zsock_close(sock);

done:
	/* Close flash area if open */
	if (context.fa != NULL) {
		flash_area_close(context.fa);
	}

	/* Post download actions */
	switch (req->action) {
#ifdef CONFIG_MCUBOOT_IMG_MANAGER
	case RPC_ENUM_FILE_ACTION_APP_IMG:
		if (boot_request_upgrade_multi(0, BOOT_UPGRADE_TEST) == 0) {
#ifdef CONFIG_INFUSE_REBOOT
			LOG_INF("DFU download complete, rebooting");
			infuse_reboot_delayed(INFUSE_REBOOT_DFU,
					      (uintptr_t)rpc_command_coap_download,
					      RPC_ENUM_FILE_ACTION_APP_IMG, K_SECONDS(2));
#else
			LOG_WRN("INFUSE_REBOOT not enabled, cannot reboot");
#endif /* CONFIG_INFUSE_REBOOT */
		}
		break;
#endif /* CONFIG_MCUBOOT_IMG_MANAGER */
#ifdef CONFIG_NRF_MODEM_LIB
	case RPC_ENUM_FILE_ACTION_NRF91_MODEM_DIFF:
		rc = infuse_dfu_nrf91_modem_delta_finish();
		if (rc > 0) {
			rc = -EIO;
		}
		if (rc == 0) {
#ifdef CONFIG_INFUSE_REBOOT
			/* Schedule the reboot in a few seconds time */
			infuse_reboot_delayed(INFUSE_REBOOT_DFU,
					      (uintptr_t)rpc_command_coap_download,
					      RPC_ENUM_FILE_ACTION_NRF91_MODEM_DIFF, K_SECONDS(2));
#else
			LOG_WRN("INFUSE_REBOOT not enabled, cannot reboot");
#endif /* CONFIG_INFUSE_REBOOT */
		} else {
			LOG_ERR("Modem DFU done failed (%d)", rc);
		}
		break;
#endif /* CONFIG_NRF_MODEM_LIB */
	}

	struct rpc_coap_download_response rsp = {
		.resource_crc = context.crc,
		.resource_len = downloaded,
	};

	return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));

error:
	struct rpc_coap_download_response err_rsp = {
		.resource_crc = 0,
		.resource_len = 0,
	};

	/* Close flash area if open */
	if (context.fa != NULL) {
		flash_area_close(context.fa);
	}

	/* Close socket if open */
	if (sock >= 0) {
		zsock_close(sock);
	}

	/* Allocate and return the response */
	return rpc_response_simple_req(request, rc, &err_rsp, sizeof(err_rsp));
}
