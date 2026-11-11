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
	/* Number of blocks to request (0 == unknown) */
	uint32_t total_blocks;
	uint16_t block_size_bytes;
	enum coap_block_size block_size;
	/* Block size can only be changed by the server on the first response */
	bool block_size_fixed;

	/* Memory for sending requests and receiving responses */
	uint16_t pkt_buffer_len;

	/* File information */
	size_t expected_size;
	size_t current_offset;
	bool final_delivered;

	/* State tracking */
	int request_timeout_ms;
	int block_requests_max;
	int block_requests_remaining;
	int error_code;

	/* Error response for a block after the next block to deliver.
	 * Only reported if that block is needed (it may be after the end of the resource).
	 */
	uint32_t error_block;
	int error_block_code;
};

#ifdef CONFIG_INFUSE_COAP_TEST_PACKET_DROP_BITMASK
static uint32_t rx_pkt_count;
#endif /* INFUSE_COAP_TEST_PACKET_DROPS */

/* Initialize download context */
static int download_context_init(struct download_context *ctx, int sock, uint8_t *working_mem,
				 size_t working_size, uint16_t req_block_size,
				 int request_timeout_ms, const char *resource, size_t file_size,
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
	if (file_size > 0) {
		/* Size provided by the caller is only used to limit requests, the download
		 * completes on the final block reported by the server.
		 */
		ctx->expected_size = file_size;
		ctx->total_blocks = DIV_ROUND_UP(file_size, ctx->block_size_bytes);
	}
	ctx->error_block = UINT32_MAX;

	ctx->chunk_callback = callback;
	ctx->user_data = user_data;

	ctx->next_token = sys_rand32_get();
	ctx->next_block_to_deliver = 0;

	/* Mark all requests as free to use */
	for (int i = 0; i < ARRAY_SIZE(ctx->requests); i++) {
		ctx->requests[i].completed = true;
	}

	/* Reserve the memory for requests and socket RX.
	 * ic_get_block_size() guarantees at least the block size plus COAP_RSP_MIN_OVERHEAD.
	 */
	__ASSERT_NO_MSG(working_size >= (ctx->block_size_bytes + COAP_RSP_MIN_OVERHEAD));
	ctx->pkt_buffer_len = MIN(working_size, COAP_RSP_OVERHEAD + ctx->block_size_bytes);
	working_mem += ctx->pkt_buffer_len;
	work_remaining -= ctx->pkt_buffer_len;

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

/* Deliver a block to the user callback, must be next_block_to_deliver */
static int deliver_block(struct download_context *ctx, uint32_t block_num, const uint8_t *data,
			 uint16_t len, bool more)
{
	int rc;

	__ASSERT_NO_MSG(block_num == ctx->next_block_to_deliver);
	LOG_DBG("Delivering block %u (%u bytes) to callback", block_num, len);

	/* Call the user's callback */
	rc = ctx->chunk_callback(ctx->current_offset, data, len, ctx->user_data);
	if (rc < 0) {
		LOG_ERR("Callback returned error: %d", rc);
		return rc;
	}

	/* Advance to next block */
	ctx->current_offset += len;
	ctx->next_block_to_deliver++;
	ctx->block_requests_remaining++;

	if (!more) {
		/* Final block of the resource */
		ctx->final_delivered = true;
		ctx->total_blocks = block_num + 1;
		LOG_INF("Last block delivered, total size: %zu bytes", ctx->current_offset);

		/* Any other buffered block is after the end of the resource */
		for (int i = 0; i < ctx->block_buffers_max; i++) {
			if (ctx->block_buffer[i].valid) {
				LOG_ERR("Block %u received after final block %u",
					ctx->block_buffer[i].block_num, block_num);
				return -EBADMSG;
			}
		}
	} else if ((ctx->total_blocks > 0) && (ctx->next_block_to_deliver >= ctx->total_blocks)) {
		/* Expected size was too small, keep requesting until the final block */
		LOG_WRN("Resource larger than expected (%u blocks)", ctx->total_blocks);
		ctx->total_blocks = ctx->next_block_to_deliver + 1;
	}
	return 0;
}

/* Deliver all consecutive blocks starting from next_block_to_deliver */
static int deliver_buffered_blocks(struct download_context *ctx)
{
	struct received_block *block;
	int rc;
	int slot;

	while (!ctx->final_delivered) {
		/* Find the next block to deliver */
		slot = -1;
		for (int i = 0; i < ctx->block_buffers_max; i++) {
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

		/* Free the slot before delivery (contents remain valid until next RX) */
		block = &ctx->block_buffer[slot];
		block->valid = false;

		rc = deliver_block(ctx, block->block_num, block->data, block->len, block->more);
		if (rc < 0) {
			return rc;
		}
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

	/* Only 1 request until the block size is fixed and we know the total size */
	max_requests =
		(ctx->block_size_fixed && (ctx->total_blocks > 0)) ? ctx->block_requests_max : 1;

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

		/* Don't request blocks after one that has already failed */
		if (ctx->next_block_to_request >= ctx->error_block) {
			continue;
		}

		/* Check if we should request a block */
		if ((ctx->total_blocks == 0) || (ctx->next_block_to_request < ctx->total_blocks)) {
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
	if (ctx->error_code < 0) {
		return;
	}

	/* The download can only progress if a request is outstanding */
	for (int i = 0; i < ARRAY_SIZE(ctx->requests); i++) {
		if (ctx->requests[i].in_flight) {
			return;
		}
	}
	LOG_ERR("No requests in flight (next block %u)", ctx->next_block_to_deliver);
	ctx->error_code = -EIO;
}

/* Process received CoAP response */
static int process_response(struct download_context *ctx, uint8_t *response_buf,
			    size_t response_len)
{
	uint8_t token_buf[COAP_TOKEN_MAX_LEN];
	struct block_request *req = NULL;
	struct coap_packet response;
	const uint8_t *payload;
	uint16_t payload_len;
	uint8_t response_code;
	uint32_t block_num;
	uint32_t token;
	bool has_more;
	int block_bytes;
	int size2;
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

	/* Get token to match with request (all requests use 4 byte tokens) */
	if (coap_header_get_token(&response, token_buf) != sizeof(token)) {
		LOG_WRN("Received response with unexpected token length");
		return 0;
	}
	memcpy(&token, token_buf, sizeof(token));

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

	/* Mark request as completed */
	req->completed = true;
	req->in_flight = false;

	/* Check response code */
	if (response_code != COAP_RESPONSE_CODE_CONTENT) {
		rc = -(100 * (response_code >> 5) + (response_code & 0x1F));
		if (req->block_num == ctx->next_block_to_deliver) {
			return rc;
		}
		/* Block may be after the end of the resource, only fail if it is needed */
		LOG_DBG("Block %u failed with %d", req->block_num, rc);
		if (req->block_num < ctx->error_block) {
			ctx->error_block = req->block_num;
			ctx->error_block_code = rc;
		}
		ctx->block_requests_remaining++;
		return 0;
	}

	/* Get payload pointer */
	payload = coap_packet_get_payload(&response, &payload_len);
	if (!payload && payload_len > 0) {
		LOG_ERR("Failed to get payload");
		return -EINVAL;
	}

	/* Parse Block2 option */
	block_bytes = coap_get_block2_option(&response, &has_more, &block_num);
	if ((block_bytes < 0) && (block_bytes != -ENOENT)) {
		LOG_ERR("Failed to parse Block2 option: %d", block_bytes);
		return -EBADMSG;
	}
	if (block_bytes == -ENOENT) {
		/* No block2 option, only valid if the entire payload is in the first packet */
		if (req->block_num != 0) {
			LOG_ERR("No Block2 option for block %u", req->block_num);
			return -EBADMSG;
		}
		LOG_DBG("No block2 option in response");
		has_more = false;
		block_num = 0;
	} else {
		if (!ctx->block_size_fixed && (block_bytes < ctx->block_size_bytes)) {
			/* Server can reduce the block size on the first response (RFC 7959 2.4).
			 * Block buffers are larger than the new size.
			 */
			LOG_INF("Server reduced block size to %d", block_bytes);
			ctx->block_size = coap_bytes_to_block_size(block_bytes);
			ctx->block_size_bytes = block_bytes;
			if (ctx->expected_size > 0) {
				ctx->total_blocks = DIV_ROUND_UP(ctx->expected_size, block_bytes);
			}
		}
		if ((block_bytes != ctx->block_size_bytes) || (block_num != req->block_num)) {
			LOG_ERR("Unexpected block %u/%d (requested %u/%d)", block_num, block_bytes,
				req->block_num, ctx->block_size_bytes);
			return -EBADMSG;
		}
		/* Only the final block can be short, truncated datagrams are also short */
		if ((payload_len > block_bytes) || (has_more && (payload_len != block_bytes))) {
			LOG_ERR("Unexpected block length %d (more %d)", payload_len, has_more);
			return -EBADMSG;
		}
	}
	ctx->block_size_fixed = true;

	/* Size reported by the server takes priority over the expected size */
	size2 = coap_get_option_int(&response, COAP_OPTION_SIZE2);
	if ((size2 > 0) && (req->block_num == 0)) {
		ctx->total_blocks = DIV_ROUND_UP(size2, ctx->block_size_bytes);
		LOG_INF("Size of download is %d bytes (%d blocks)", size2, ctx->total_blocks);
	}

	LOG_DBG("Received block %u (%u bytes, more=%d)", block_num, payload_len, has_more);

	if (block_num == ctx->next_block_to_deliver) {
		/* Deliver immediately, then any buffered blocks that are now in sequence */
		rc = deliver_block(ctx, block_num, payload, payload_len, has_more);
		if (rc < 0) {
			return rc;
		}
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
			return rc;
		}
	}

	/* Fail if the next block to deliver has already failed */
	if (!ctx->final_delivered && (ctx->next_block_to_deliver >= ctx->error_block)) {
		return ctx->error_block_code;
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
		if (rc < 0) {
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
	/* Download is complete once the final block has been delivered */
	return ctx->final_delivered;
}

/* Main download function */
int infuse_coap_download(int socket, const char *resource, size_t file_size,
			 infuse_coap_data_cb data_cb, void *user_context, uint8_t *working_mem,
			 size_t working_size, uint16_t req_block_size, int timeout_ms)
{
	struct zsock_pollfd pollfds = {
		.fd = socket,
		.events = ZSOCK_POLLIN,
	};
	struct download_context ctx;
	int rc;

	/* Setup download context */
	rc = download_context_init(&ctx, socket, working_mem, working_size, req_block_size,
				   timeout_ms, resource, file_size, data_cb, user_context);
	if (rc < 0) {
		return rc;
	}

	LOG_INF("Downloading: %s (Block size %d)", resource, ctx.block_size_bytes);

	/* Main download loop */
	while (!is_download_complete(&ctx) && (ctx.error_code == 0)) {
		/* Queue any requests we can */
		queue_requests(&ctx, working_mem, ctx.pkt_buffer_len);
		if (ctx.error_code < 0) {
			continue;
		}

		/* Wait for response */
		receive_data(&ctx, &pollfds, working_mem, ctx.pkt_buffer_len);
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

	LOG_INF("Download complete: %zu bytes delivered via callback", ctx.current_offset);

	return ctx.current_offset;
}
