/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
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

#ifdef CONFIG_NET_IPV4_MTU
/* If the MTU is explicitly defined, check the size before trying to use 1KB blocks */
#define MTU_SUPPORTS_1KB (CONFIG_NET_IPV4_MTU >= (1024 + 64))
#else
/* Assume it is supported until a configuration proves otherwise */
#define MTU_SUPPORTS_1KB 1
#endif

#define COAP_RSP_OVERHEAD 64

LOG_MODULE_REGISTER(infuse_coap, CONFIG_INFUSE_COAP_LOG_LEVEL);

/* Structure to hold received block data temporarily */
struct received_block {
	uint8_t *data;
	uint32_t block_num;
	uint16_t len;
	bool valid;
	bool more;
};

/* Structure to track individual block requests */
struct block_request {
	int64_t timeout;
	uint32_t block_num;
	uint32_t token;
	uint8_t retries;
	bool in_flight;
	bool completed;
};

/* Main download context */
struct download_context {
	/* TX/RX socket */
	int sock;

	/* Callback for delivering chunks */
	infuse_coap_data_cb chunk_callback;
	void *user_data;

	/* Request tracking */
	struct block_request requests[CONFIG_INFUSE_COAP_BACKEND_PARALLEL_MAX_IN_FLIGHT];

	/* Block buffering for in-order delivery */
	struct received_block block_buffer[CONFIG_INFUSE_COAP_BACKEND_PARALLEL_MAX_IN_FLIGHT];
	uint32_t next_block_to_deliver;
	uint8_t block_buffers_max;

	/* Resource metadata */
	const char *resource;
	uint8_t resource_split[CONFIG_INFUSE_COAP_MAX_URI_SEGMENTS + 1];
	int8_t num_resource_split;

	/* Request block information */
	uint32_t next_token;
	uint32_t next_block_to_request;
	uint32_t total_blocks;
	uint16_t block_size_bytes;
	enum coap_block_size block_size;

	/* File information */
	size_t total_size;
	size_t current_offset;

	/* State tracking */
	int request_timeout_ms;
	int block_requests_max;
	int block_requests_remaining;
	int error_code;
};

#ifdef CONFIG_INFUSE_COAP_TEST_PACKET_DROP_BITMASK
static uint32_t rx_pkt_count;
#endif /* INFUSE_COAP_TEST_PACKET_DROPS */

/* Initialize download context */
static int download_context_init(struct download_context *ctx, int sock, uint8_t *working_mem,
				 size_t working_size, uint16_t req_block_size,
				 int request_timeout_ms, const char *resource,
				 infuse_coap_data_cb callback, void *user_data)
{
	size_t work_remaining = working_size;
	int rc;

	memset(ctx, 0, sizeof(*ctx));

	if (callback == NULL) {
		LOG_ERR("Chunk callback is required");
		return -EINVAL;
	}

	/* Pre-split the resource path into components */
	ctx->resource = resource;
	ctx->num_resource_split = ic_resource_path_split(resource, ctx->resource_split,
							 ARRAY_SIZE(ctx->resource_split));
	if (ctx->num_resource_split < 0) {
		LOG_ERR("Failed to split resource path");
		return -EINVAL;
	}

	rc = ic_get_block_size(working_size, req_block_size, &ctx->block_size);
	if (rc < 0) {
		return rc;
	}
	ctx->block_size_bytes = coap_block_size_to_bytes(ctx->block_size);
	ctx->request_timeout_ms = request_timeout_ms;

	ctx->sock = sock;

	ctx->chunk_callback = callback;
	ctx->user_data = user_data;

	ctx->next_token = sys_rand32_get();
	ctx->next_block_to_deliver = 0;

	/* Mark all requests as free to use */
	for (int i = 0; i < ARRAY_SIZE(ctx->requests); i++) {
		ctx->requests[i].completed = true;
	}

	/* Reserve the memory for requests and socket RX */
	working_mem += (COAP_RSP_OVERHEAD + ctx->block_size_bytes);
	work_remaining -= (COAP_RSP_OVERHEAD + ctx->block_size_bytes);

	/* Assign memory to the block buffers */
	while ((work_remaining >= ctx->block_size_bytes) &&
	       (ctx->block_buffers_max < ARRAY_SIZE(ctx->block_buffer))) {

		ctx->block_buffer[ctx->block_buffers_max].data = working_mem;

		working_mem += ctx->block_size_bytes;
		work_remaining -= ctx->block_size_bytes;
		ctx->block_buffers_max++;
	}
	/* We can always have one more request in-flight than the number of block buffers we have,
	 * as one pending request will always be the next block to deliver immediately without the
	 * need to buffer.
	 */
	ctx->block_requests_remaining = 1 + ctx->block_buffers_max;
	ctx->block_requests_remaining =
		MIN(ctx->block_requests_remaining, ARRAY_SIZE(ctx->requests));
	ctx->block_requests_max = ctx->block_requests_remaining;
	LOG_DBG("%d bytes gives memory for %d parallel requests of %d bytes", working_size,
		ctx->block_requests_max, ctx->block_size_bytes);
	return 0;
}

/* Find a free slot in the block buffer */
static int find_free_buffer_slot(struct download_context *ctx)
{
	for (int i = 0; i < ctx->block_buffers_max; i++) {
		if (!ctx->block_buffer[i].valid) {
			__ASSERT_NO_MSG(ctx->block_buffer[i].data != NULL);
			return i;
		}
	}
	return -1;
}

/* Store a received block in the buffer */
static int buffer_block(struct download_context *ctx, uint32_t block_num, const uint8_t *data,
			uint16_t len, bool more)
{
	int slot;

	slot = find_free_buffer_slot(ctx);
	if (slot < 0) {
		LOG_ERR("Block buffer full, cannot buffer block %u", block_num);
		return -ENOMEM;
	}

	ctx->block_buffer[slot].block_num = block_num;
	ctx->block_buffer[slot].len = len;
	ctx->block_buffer[slot].more = more;
	memcpy(ctx->block_buffer[slot].data, data, len);
	ctx->block_buffer[slot].valid = true;

	LOG_DBG("Buffered block %u in slot %d", block_num, slot);

	return 0;
}

/* Deliver all consecutive blocks starting from next_block_to_deliver */
static int deliver_buffered_blocks(struct download_context *ctx)
{
	struct received_block *block;
	int rc = 0;
	int slot;

	while (true) {
		/* Find the next block to deliver */
		slot = -1;
		for (int i = 0; i < ARRAY_SIZE(ctx->block_buffer); i++) {
			if (ctx->block_buffer[i].valid &&
			    ctx->block_buffer[i].block_num == ctx->next_block_to_deliver) {
				slot = i;
				break;
			}
		}
		if (slot < 0) {
			/* Next block not yet received */
			break;
		}

		block = &ctx->block_buffer[slot];

		LOG_DBG("Delivering block %u (%u bytes) to callback", block->block_num, block->len);

		/* Call the user's callback */
		rc = ctx->chunk_callback(ctx->current_offset, block->data, block->len,
					 ctx->user_data);
		if (rc < 0) {
			LOG_ERR("Callback returned error: %d", rc);
			ctx->error_code = rc;
			return rc;
		}

		/* Update total size if this was the last block */
		if (!block->more) {
			ctx->total_size = ctx->current_offset + block->len;
			ctx->total_blocks = block->block_num + 1;
			LOG_INF("Last block delivered, total size: %zu bytes", ctx->total_size);
		}

		/* Mark slot as free and advance to next block */
		block->valid = false;
		ctx->current_offset += block->len;
		ctx->next_block_to_deliver++;
		ctx->block_requests_remaining++;
	}

	return 0;
}

/* Build CoAP GET request with block2 option */
static int build_block_request(struct download_context *ctx, uint32_t block_num, uint32_t token,
			       uint8_t *buf, size_t buf_len)
{
	struct coap_packet request;
	int rc;

	rc = coap_packet_init(&request, buf, buf_len, COAP_VERSION_1, COAP_TYPE_CON, sizeof(token),
			      (uint8_t *)&token, COAP_METHOD_GET, coap_next_id());
	if (rc < 0) {
		LOG_ERR("Failed to init CoAP packet: %d", rc);
		return rc;
	}

	/* Resource path is arbitrary length so can fail */
	rc = ic_resource_path_append(&request, ctx->resource, ctx->resource_split,
				     ctx->num_resource_split);
	if (rc < 0) {
		LOG_ERR("Path append failure");
		return rc;
	}

	/* Build block context */
	struct coap_block_context block_ctx = {
		.block_size = ctx->block_size,
		.current = block_num * ctx->block_size_bytes,
		.total_size = ctx->total_size,
	};

	rc = coap_append_block2_option(&request, &block_ctx);
	if (rc < 0) {
		LOG_ERR("Failed to add Block2 option: %d", rc);
		return rc;
	}

	return request.offset;
}

/* Send a block request */
static int send_block_request(struct download_context *ctx, struct block_request *req,
			      uint8_t *buffer, uint16_t buffer_len)
{
	int sent;
	int len;

	/* Create the request buffer */
	len = build_block_request(ctx, req->block_num, req->token, buffer, buffer_len);
	if (len < 0) {
		return len;
	}

	/* Send the request buffer */
	sent = zsock_send(ctx->sock, buffer, len, 0);
	if (sent < 0) {
		LOG_ERR("Failed to send request: %d", errno);
		return -errno;
	}

	/* Store state and timeout information */
	req->in_flight = true;
	req->timeout = k_uptime_get() + ctx->request_timeout_ms;
	LOG_DBG("Sent request for block %u (token 0x%08x)", req->block_num, req->token);
	return 0;
}

static void handle_request_timeout(struct download_context *ctx, struct block_request *req,
				   uint8_t *tx_buffer, uint16_t tx_buffer_len)
{
	int rc;

	if (req->in_flight) {
		/* Still waiting for the response or timeout */
		return;
	}

	/* Generate new token, resend */
	req->token = ctx->next_token++;
	rc = send_block_request(ctx, req, tx_buffer, tx_buffer_len);
	if (rc < 0) {
		ctx->error_code = rc;
	}
}

static void queue_requests(struct download_context *ctx, uint8_t *tx_buffer, uint16_t tx_buffer_len)
{
	struct block_request *req;
	int max_requests;
	int rc;

	/* Only 1 request until we know the total size */
	max_requests = ctx->total_size == 0 ? 1 : ctx->block_requests_max;

	/* Send new requests for available slots */
	for (int i = 0; i < max_requests; i++) {
		req = &ctx->requests[i];
		LOG_DBG("Slot %d: (Flight %d) (Complete %d) (Block %d) (Requests %d)", i,
			req->in_flight, req->completed, req->block_num,
			ctx->block_requests_remaining);

		if (!req->completed) {
			handle_request_timeout(ctx, req, tx_buffer, tx_buffer_len);
			continue;
		}

		/* Check if the request storage is already in use */
		if (req->in_flight) {
			/* Still waiting for response */
			continue;
		}

		/* Ensure we have space to buffer a response that can't be delivered immediately */
		if (ctx->block_requests_remaining == 0) {
			LOG_DBG("Block buffer full");
			/* Don't exit, other requests may have timed out */
			continue;
		}

		/* Check if we should request a block */
		if ((ctx->total_size == 0) || (ctx->next_block_to_request < ctx->total_blocks)) {
			req->block_num = ctx->next_block_to_request++;
			req->token = ctx->next_token++;
			req->retries = 0;
			req->completed = false;

			rc = send_block_request(ctx, &ctx->requests[i], tx_buffer, tx_buffer_len);
			if (rc < 0) {
				ctx->error_code = rc;
				break;
			}

			/* One of our block requests has been consumed */
			ctx->block_requests_remaining--;
		}
	}
}

/* Process received CoAP response */
static int process_response(struct download_context *ctx, uint8_t *response_buf,
			    size_t response_len)
{
	struct block_request *req = NULL;
	struct coap_packet response;
	const uint8_t *payload;
	uint16_t payload_len;
	uint8_t response_code;
	uint32_t token;
	uint8_t code;
	int rc;

	/* Initial response parsing */
	rc = coap_packet_parse(&response, response_buf, response_len, NULL, 0);
	if (rc < 0) {
		LOG_ERR("Failed to parse CoAP response: %d", rc);
		return rc;
	}

	/* Handle empty response (by ignoring) */
	response_code = coap_header_get_code(&response);
	if (response_code == COAP_CODE_EMPTY) {
		LOG_DBG("Empty response, ignore");
		return 0;
	}

	/* Get token to match with request */
	payload_len = coap_header_get_token(&response, (void *)&token);
	if (payload_len != sizeof(token)) {
		LOG_ERR("Invalid token length %d %d", payload_len, sizeof(token));
		return -EINVAL;
	}

	/* Find matching request */
	for (int i = 0; i < CONFIG_INFUSE_COAP_BACKEND_PARALLEL_MAX_IN_FLIGHT; i++) {
		if (!ctx->requests[i].completed && ctx->requests[i].token == token) {
			LOG_DBG("Received token: 0x%08x (Slot %d)", token, i);
			req = &ctx->requests[i];
			break;
		}
	}
	if (!req) {
		/* No known response, not a fatal error */
		LOG_WRN("Received response for unknown token 0x%x", token);
		return 0;
	}

	/* Check response code */
	code = coap_header_get_code(&response);
	if (code != COAP_RESPONSE_CODE_CONTENT) {
		return -(100 * (code >> 5) + (code & 0x1F));
	}

	/* Parse Block2 option */
	uint32_t block_num;
	bool has_more;
	int size2;

	rc = coap_get_block2_option(&response, &has_more, &block_num);
	if (rc == -ENOENT) {
		/* No block2 option, assume entire payload exists in the first packet */
		LOG_DBG("No block2 option in response");
		has_more = false;
		block_num = 0;
	} else if (rc < 0) {
		LOG_ERR("Failed to parse Block2 option: %d", rc);
		return rc;
	}

	/* Get payload pointer */
	payload = coap_packet_get_payload(&response, &payload_len);
	if (!payload && payload_len > 0) {
		LOG_ERR("Failed to get payload");
		return -EINVAL;
	}

	/* Set size knowledge if not known */
	size2 = coap_get_option_int(&response, COAP_OPTION_SIZE2);
	if ((ctx->total_size == 0) && (size2 != -ENOENT)) {
		LOG_INF("Size of download is %d bytes", size2);
		ctx->total_size = size2;
		ctx->total_blocks = ROUND_UP(size2, ctx->block_size_bytes) / ctx->block_size_bytes;
	}

	LOG_DBG("Received block %u (%u bytes, more=%d)", block_num, payload_len, has_more);

	/* Mark request as completed */
	req->completed = true;
	req->in_flight = false;

	/* Check if this is the next block to deliver */
	if (block_num == ctx->next_block_to_deliver) {
		/* Deliver immediately */
		LOG_DBG("Delivering block %u immediately (in order)", block_num);

		rc = ctx->chunk_callback(ctx->current_offset, payload, payload_len, ctx->user_data);
		if (rc < 0) {
			LOG_ERR("Callback returned error: %d", rc);
			ctx->error_code = rc;
			return rc;
		}

		/* Update total size if this was the last block */
		if (!has_more) {
			ctx->total_size = ctx->current_offset + payload_len;
			ctx->total_blocks = block_num + 1;
			LOG_INF("Last block delivered, total size: %zu bytes", ctx->total_size);
		}

		ctx->block_requests_remaining++;
		ctx->current_offset += payload_len;
		ctx->next_block_to_deliver++;

		/* Try to deliver any buffered blocks that are now in sequence */
		rc = deliver_buffered_blocks(ctx);
		if (rc < 0) {
			return rc;
		}
	} else {
		/* Buffer this block for later delivery */
		LOG_DBG("Block %u received out of order (expecting %u), buffering", block_num,
			ctx->next_block_to_deliver);

		rc = buffer_block(ctx, block_num, payload, payload_len, has_more);
		if (rc < 0) {
			ctx->error_code = rc;
			return rc;
		}
	}

	return 0;
}

static void receive_data(struct download_context *ctx, struct zsock_pollfd *pollfds,
			 uint8_t *rx_buffer, uint16_t rx_len)
{
	int received;
	int rc;

	/* Wait for data to be available */
	rc = zsock_poll(pollfds, 1, ctx->request_timeout_ms);
	if (rc < 0) {
		LOG_ERR("Poll error (%d)", -errno);
		ctx->error_code = rc;
		return;
	}
	if (pollfds[0].revents & (ZSOCK_POLLHUP | ZSOCK_POLLNVAL)) {
		LOG_ERR("Socket closed");
		ctx->error_code = -EBADF;
	} else if (pollfds[0].revents & ZSOCK_POLLIN) {
		/* Read data into provided buffer */
		received = zsock_recv(ctx->sock, rx_buffer, rx_len, 0);
		if (received < 0) {
			LOG_ERR("No data");
			ctx->error_code = -EIO;
			return;
		}

#ifdef CONFIG_INFUSE_COAP_TEST_PACKET_DROP_BITMASK
		if (BIT(rx_pkt_count % 32) & CONFIG_INFUSE_COAP_TEST_PACKET_DROP_BITMASK) {
			LOG_WRN("ZTEST: Dropping RX packet %d", rx_pkt_count);
			rx_pkt_count++;
			return;
		}
		rx_pkt_count++;
#endif /* CONFIG_INFUSE_COAP_TEST_PACKET_DROP_BITMASK */

		/* Handle received data */
		rc = process_response(ctx, rx_buffer, received);
		if (rc < 0 && rc != -ENOENT) {
			ctx->error_code = rc;
		}
	}
}

static void handle_timeouts(struct download_context *ctx)
{
	int64_t now = k_uptime_get();

	for (int i = 0; i < CONFIG_INFUSE_COAP_BACKEND_PARALLEL_MAX_IN_FLIGHT; i++) {
		if (ctx->requests[i].in_flight && (now > ctx->requests[i].timeout)) {
			if (ctx->requests[i].retries >= CONFIG_INFUSE_COAP_MAX_TIMEOUTS) {
				LOG_ERR("Max retries exceeded for block %u",
					ctx->requests[i].block_num);
				ctx->error_code = -ETIMEDOUT;
			} else {
				LOG_WRN("Timeout for block %u, retrying",
					ctx->requests[i].block_num);
				ctx->requests[i].retries++;
				ctx->requests[i].in_flight = false;
			}
		}
	}
}

static bool is_download_complete(struct download_context *ctx)
{
	/* Download is complete when we know the total size and have delivered all blocks */
	if (ctx->total_size == 0) {
		return false;
	}

	/* Check if we've delivered all blocks (no gaps, no buffered blocks) */
	if (ctx->next_block_to_deliver < ctx->total_blocks) {
		return false;
	}

	/* Verify no blocks are still buffered */
	for (int i = 0; i < CONFIG_INFUSE_COAP_BACKEND_PARALLEL_MAX_IN_FLIGHT; i++) {
		if (ctx->block_buffer[i].valid) {
			return false;
		}
	}

	/* All checks passed */
	return true;
}

/* Main download function */
int infuse_coap_download(int socket, const char *resource, infuse_coap_data_cb data_cb,
			 void *user_context, uint8_t *working_mem, size_t working_size,
			 uint16_t req_block_size, int timeout_ms)
{
	struct zsock_pollfd pollfds = {
		.fd = socket,
		.events = ZSOCK_POLLIN,
	};
	struct download_context ctx;
	int rc;

	/* Setup download context */
	rc = download_context_init(&ctx, socket, working_mem, working_size, req_block_size,
				   timeout_ms, resource, data_cb, user_context);
	if (rc < 0) {
		return rc;
	}

	LOG_INF("Downloading: %s (Block size %d)", resource, ctx.block_size_bytes);

	/* Main download loop */
	while (!is_download_complete(&ctx) && (ctx.error_code == 0)) {
		/* Queue any requests we can */
		queue_requests(&ctx, working_mem, working_size);
		if (ctx.error_code < 0) {
			continue;
		}

		/* Wait for response */
		receive_data(&ctx, &pollfds, working_mem, working_size);
		if (ctx.error_code < 0) {
			continue;
		}

		/* Check for timeouts and retries */
		handle_timeouts(&ctx);
	}

	if (ctx.error_code < 0) {
		LOG_ERR("Download failed with error: %d", ctx.error_code);
		return ctx.error_code;
	}

	LOG_INF("Download complete: %zu bytes delivered via callback", ctx.total_size);

	return ctx.total_size;
}
