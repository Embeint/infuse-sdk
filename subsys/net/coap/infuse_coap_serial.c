/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/__assert.h>

#include <infuse/net/coap.h>

#include "common.h"

BUILD_ASSERT(COAP_TOKEN_MAX_LEN == sizeof(uint64_t));

#ifdef CONFIG_INFUSE_COAP_TEST_PACKET_DROP_BITMASK
static uint32_t rx_pkt_count;
#endif /* INFUSE_COAP_TEST_PACKET_DROPS */

LOG_MODULE_REGISTER(infuse_coap, CONFIG_INFUSE_COAP_LOG_LEVEL);

/* Validate that a response contains the requested block before it is used */
static int validate_block(const struct coap_packet *reply, const struct coap_block_context *ctx,
			  uint16_t payload_len)
{
	uint32_t block_num;
	bool has_more;
	int size;

	size = coap_get_block2_option(reply, &has_more, &block_num);
	if (size == -ENOENT) {
		/* No Block2 option, only valid if the entire resource is in this response */
		if (ctx->current != 0) {
			LOG_ERR("No Block2 option at offset %zu", ctx->current);
			return -EBADMSG;
		}
		return 0;
	}
	if (size < 0) {
		LOG_ERR("Invalid Block2 option (%d)", size);
		return -EBADMSG;
	}
	/* Server can reduce the block size, but not increase it (RFC 7959, section 2.4) */
	if (size > coap_block_size_to_bytes(ctx->block_size)) {
		LOG_ERR("Block size increased to %d", size);
		return -EBADMSG;
	}
	if (((size_t)block_num * size) != ctx->current) {
		LOG_ERR("Unexpected block %u (size %d, offset %zu)", block_num, size, ctx->current);
		return -EBADMSG;
	}
	/* Only the final block can be short, truncated datagrams are also short */
	if ((payload_len > size) || (has_more && (payload_len != size))) {
		LOG_ERR("Unexpected block length %d (size %d, more %d)", payload_len, size,
			has_more);
		return -EBADMSG;
	}
	return 0;
}

int infuse_coap_download(int socket, const char *resource, size_t file_size,
			 infuse_coap_data_cb data_cb, void *user_context, uint8_t *working_mem,
			 size_t working_size, uint16_t req_block_size, int timeout_ms)
{
	struct coap_block_context blk_ctx;
	struct coap_packet request, reply;
	struct zsock_pollfd pollfds[1];
	uint16_t pkt_len = MIN(working_size, UINT16_MAX);
	uint8_t rx_token[COAP_TOKEN_MAX_LEN];
	uint64_t tx_token;
	size_t next_block = UINT32_MAX;
	int received, total_received = 0;
	enum coap_block_size block_size;
	uint8_t chunk_retries = 0;
	uint8_t response_code;
	uint16_t pkt_id;
	int rc;

	if (data_cb == NULL) {
		return -EINVAL;
	}

	rc = ic_get_block_size(working_size, req_block_size, &block_size);
	if (rc < 0) {
		return rc;
	}

	LOG_INF("Downloading: %s (Block size %d)", resource, block_size);

	/* Pre-split the resource path into components */
	uint8_t path_split[CONFIG_INFUSE_COAP_MAX_URI_SEGMENTS + 1] = {0};
	int num_paths = ic_resource_path_split(resource, path_split, ARRAY_SIZE(path_split));

	if (num_paths < 0) {
		LOG_ERR("Failed to split resource path");
		return -EINVAL;
	}

	pollfds[0].fd = socket;
	pollfds[0].events = ZSOCK_POLLIN;

	/* The expected size is not passed to the block context, as it would cause a mismatching
	 * server size to fail the download, or silently truncate it. Callers validate the result.
	 */
	ARG_UNUSED(file_size);
	coap_block_transfer_init(&blk_ctx, block_size, 0);

	while (next_block) {
		/* Minimum work area size should guarantee adding these headers cannot fail */
		sys_rand_get(&tx_token, sizeof(tx_token));
		pkt_id = coap_next_id();
		rc = coap_packet_init(&request, working_mem, pkt_len, COAP_VERSION_1, COAP_TYPE_CON,
				      COAP_TOKEN_MAX_LEN, (uint8_t *)&tx_token, COAP_METHOD_GET,
				      pkt_id);
		__ASSERT_NO_MSG(rc == 0);
		rc = coap_append_block2_option(&request, &blk_ctx);
		__ASSERT_NO_MSG(rc == 0);

		/* Resource path is arbitrary length so can fail */
		rc = ic_resource_path_append(&request, resource, path_split, num_paths);
		if (rc < 0) {
			LOG_ERR("Path append failure");
			return rc;
		}

		rc = zsock_send(socket, request.data, request.offset, 0);
		if (rc < 0) {
			LOG_ERR("zsock_send failure (%d)", -errno);
			return -errno;
		}

poll_retry:
		/* Wait for response */
		rc = zsock_poll(pollfds, 1, timeout_ms);
		if (rc < 0) {
			LOG_ERR("Poll error (%d)", -errno);
			return -errno;
		}
		if (pollfds[0].revents & (ZSOCK_POLLHUP | ZSOCK_POLLNVAL)) {
			LOG_ERR("Socket closed");
			return -EBADF;
		}
		if (!(pollfds[0].revents & ZSOCK_POLLIN)) {
			LOG_WRN("Poll timeout");
			if (++chunk_retries >= CONFIG_INFUSE_COAP_MAX_TIMEOUTS) {
				LOG_ERR("Giving up");
				return -ETIMEDOUT;
			}
			/* Start from top of loop again */
			continue;
		}
		received = zsock_recv(socket, working_mem, pkt_len, ZSOCK_MSG_DONTWAIT);
		if (received < 0) {
			LOG_ERR("Receive error (%d)", -errno);
			return -EIO;
		}
		if (received == 0) {
			/* Empty datagram */
			goto poll_retry;
		}
#ifdef CONFIG_INFUSE_COAP_TEST_PACKET_DROP_BITMASK
		if (BIT(rx_pkt_count % 32) & CONFIG_INFUSE_COAP_TEST_PACKET_DROP_BITMASK) {
			LOG_WRN("ZTEST: Dropping RX packet %d", rx_pkt_count);
			rx_pkt_count++;
			goto poll_retry;
		}
		rx_pkt_count++;
#endif /* CONFIG_INFUSE_COAP_TEST_PACKET_DROP_BITMASK */
		rc = coap_packet_parse(&reply, working_mem, received, NULL, 0);
		if (rc < 0) {
			LOG_WRN("Invalid data received (%d)", rc);
			goto poll_retry;
		}
		response_code = coap_header_get_code(&reply);

		/* Empty response check first */
		if (response_code == COAP_CODE_EMPTY) {
			LOG_INF("Empty response, retrying poll");
			goto poll_retry;
		}

		/* Compare response token to request token */
		if ((coap_header_get_token(&reply, rx_token) != sizeof(tx_token)) ||
		    (memcmp(rx_token, &tx_token, sizeof(tx_token)) != 0)) {
			LOG_WRN("Mismatched token");
			goto poll_retry;
		}

		/* Validate response code */
		if (response_code != COAP_RESPONSE_CODE_CONTENT) {
			rc = (100 * (response_code >> 5) + (response_code & 0x1F));
			LOG_ERR("Response code %d", rc);
			return -rc;
		}

		/* Reset retry counter */
		chunk_retries = 0;

		/* Validate the payload before it is used */
		const uint8_t *payload;
		uint16_t payload_len;

		payload = coap_packet_get_payload(&reply, &payload_len);
		rc = validate_block(&reply, &blk_ctx, payload_len);
		if (rc < 0) {
			return rc;
		}

		/* Update block context from the response */
		rc = coap_update_from_block(&reply, &blk_ctx);
		if (rc < 0) {
			LOG_ERR("coap_update_from_block %d", rc);
			return -EIO;
		}

		/* Run user callback */
		LOG_DBG("RX: %d PAYLOAD: %d", received, payload_len);
		rc = data_cb(blk_ctx.current, payload, payload_len, user_context);
		if (rc != 0) {
			return rc;
		}
		total_received += payload_len;

		/* Get next block address */
		next_block = coap_next_block(&reply, &blk_ctx);
	}
	LOG_DBG("Download complete");
	return total_received;
}
