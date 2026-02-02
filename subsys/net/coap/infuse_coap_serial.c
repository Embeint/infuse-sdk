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

int infuse_coap_download(int socket, const char *resource, infuse_coap_data_cb data_cb,
			 void *user_context, uint8_t *working_mem, size_t working_size,
			 uint16_t req_block_size, int timeout_ms)
{
	struct coap_block_context blk_ctx;
	struct coap_packet request, reply;
	struct zsock_pollfd pollfds[1];
	uint64_t tx_token, rx_token;
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

	coap_block_transfer_init(&blk_ctx, block_size, 0);

	while (next_block) {
		/* Minimum work area size should gaurantee adding these headers cannot fail */
		sys_rand_get(&tx_token, sizeof(tx_token));
		pkt_id = coap_next_id();
		rc = coap_packet_init(&request, working_mem, working_size, COAP_VERSION_1,
				      COAP_TYPE_CON, COAP_TOKEN_MAX_LEN, (uint8_t *)&tx_token,
				      COAP_METHOD_GET, pkt_id);
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
		received = zsock_recv(socket, working_mem, working_size, ZSOCK_MSG_DONTWAIT);
		if (received <= 0) {
			LOG_ERR("No data");
			return -EIO;
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
			LOG_ERR("Invalid data received");
		}
		response_code = coap_header_get_code(&reply);

		/* Empty response check first */
		if (response_code == COAP_CODE_EMPTY) {
			LOG_INF("Empty response, retrying poll");
			goto poll_retry;
		}

		/* Compare response token to request token */
		coap_header_get_token(&reply, (uint8_t *)&rx_token);
		if (tx_token != rx_token) {
			LOG_WRN("Mismatched token (%llu != %llu)", tx_token, rx_token);
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

		/* Extract payload and run user callback */
		const uint8_t *payload;
		uint16_t payload_len;

		payload = coap_packet_get_payload(&reply, &payload_len);
		total_received += payload_len;
		LOG_DBG("RX: %d PAYLOAD: %d", received, payload_len);

		rc = data_cb(blk_ctx.current, payload, payload_len, user_context);
		if (rc != 0) {
			return rc;
		}

		/* Update expected next block */
		rc = coap_update_from_block(&reply, &blk_ctx);
		if (rc < 0) {
			LOG_ERR("coap_update_from_block %d", rc);
			return -EIO;
		}
		/* Get next block address */
		next_block = coap_next_block(&reply, &blk_ctx);
	}
	LOG_DBG("Download complete");
	return total_received;
}
