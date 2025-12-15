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

BUILD_ASSERT(COAP_TOKEN_MAX_LEN == sizeof(uint64_t));

#ifdef CONFIG_NET_IPV4_MTU
/* If the MTU is explicitly defined, check the size before trying to use 1KB blocks */
#define MTU_SUPPORTS_1KB (CONFIG_NET_IPV4_MTU >= (1024 + 64))
#else
/* Assume it is supported until a configuration proves otherwise */
#define MTU_SUPPORTS_1KB 1
#endif

LOG_MODULE_REGISTER(infuse_coap, CONFIG_INFUSE_COAP_LOG_LEVEL);

/* Determine the locations of '/' characters and encode into array */
static int resource_path_split(const char *resource, uint8_t *component_starts, uint8_t array_len)
{
	uint8_t num_paths = 1;
	int i = 0;

	/* Scan through string for the '/' character */
	while (resource[i] != '\0') {
		if (resource[i] == '/') {
			if (num_paths == (array_len - 1)) {
				/* Too many path splits */
				return -EINVAL;
			}
			/* Store the start of the next component */
			component_starts[num_paths++] = i + 1;
		}
		i++;
	}
	/* Add the end of the string with a hypothetical next component */
	component_starts[num_paths] = i + 1;
	return num_paths;
}

/* Append resource path using splits from `resource_path_split` */
static int resource_path_append(struct coap_packet *request, const char *resource,
				uint8_t *component_starts, uint8_t num_components)
{
	int rc;

	/* Add all path components to packet */
	for (int i = 0; i < num_components; i++) {
		const char *comp_start = resource + component_starts[i];
		int comp_len = component_starts[i + 1] - component_starts[i] - 1;

		rc = coap_packet_append_option(request, COAP_OPTION_URI_PATH, comp_start, comp_len);
		if (rc < 0) {
			return rc;
		}
	}
	return 0;
}

int infuse_coap_download(int socket, const char *resource, infuse_coap_data_cb data_cb,
			 void *user_context, uint8_t *working_mem, size_t working_size,
			 int timeout_ms)
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

	/* Determine block size */
	if (MTU_SUPPORTS_1KB && (working_size >= (1024 + 64))) {
		block_size = COAP_BLOCK_1024;
	} else if (working_size >= (512 + 64)) {
		block_size = COAP_BLOCK_512;
	} else if (working_size >= (256 + 64)) {
		block_size = COAP_BLOCK_256;
	} else if (working_size >= (128 + 64)) {
		block_size = COAP_BLOCK_128;
	} else if (working_size >= 128) {
		block_size = COAP_BLOCK_64;
	} else {
		return -ENOMEM;
	}

	LOG_INF("Downloading: %s (Block size %d)", resource, block_size);

	/* Pre-split the resource path into components */
	uint8_t path_split[CONFIG_INFUSE_COAP_MAX_URI_SEGMENTS + 1] = {0};
	int num_paths = resource_path_split(resource, path_split, ARRAY_SIZE(path_split));

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
		rc = resource_path_append(&request, resource, path_split, num_paths);
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
