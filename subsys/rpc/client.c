/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/__assert.h>

#include <infuse/rpc/client.h>
#include <infuse/rpc/types.h>

#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/types.h>

LOG_MODULE_REGISTER(rpc_client, CONFIG_INFUSE_RPC_LOG_LEVEL);

static struct rpc_client_cmd_ctx *find_cmd_ctx(struct rpc_client_ctx *ctx, uint32_t request_id)
{
	for (int i = 0; i < ARRAY_SIZE(ctx->cmd_ctx); i++) {
		struct rpc_client_cmd_ctx *c = &ctx->cmd_ctx[i];

		if (c->request_id == request_id) {
			return c;
		}
	}
	return NULL;
}

static void run_callback(struct rpc_client_ctx *ctx, const struct net_buf *buf, uint16_t command_id,
			 uint32_t request_id)
{
	struct rpc_client_cmd_ctx *c = find_cmd_ctx(ctx, request_id);

	if (c == NULL) {
		LOG_ERR("Unknown RPC_RSP: CMD=%d ID=0x%08X", command_id, request_id);
		return;
	}
	if (command_id != c->command_id) {
		LOG_WRN("Mismatched command ID (%d != %d)", command_id, c->command_id);
		return;
	}

	/* Terminate the response timeout */
	k_timer_stop(&c->timeout);

	/* Cache context information */
	rpc_client_rsp_fn fn = c->cb;
	void *user_data = c->user_data;

	/* Free the context information.
	 * Performing this after the callback is incorrect
	 * as the callback may lead to `rpc_client_cleanup`
	 * before we get the chance to give the context semaphore.
	 */
	c->request_id = 0;
	k_sem_give(&c->ack);
	k_sem_give(&ctx->cmd_ctx_sem);

	/* Run the callback */
	fn(buf, user_data);
}

static void command_timeout(struct k_timer *timer)
{
	struct rpc_client_ctx *ctx = timer->user_data;
	struct rpc_client_cmd_ctx *c = CONTAINER_OF(timer, struct rpc_client_cmd_ctx, timeout);

	LOG_WRN("Timeout request %08X", c->request_id);
	run_callback(ctx, NULL, c->command_id, c->request_id);
}

static bool packet_received(struct net_buf *buf, bool decrypted, void *user_ctx)
{
	struct epacket_rx_metadata *meta = net_buf_user_data(buf);
	struct rpc_client_ctx *ctx = user_ctx;

	if (!decrypted) {
		return true;
	}
	if (meta->type == INFUSE_RPC_DATA_ACK) {
		struct infuse_rpc_data_ack *ack = (void *)buf->data;
		struct rpc_client_cmd_ctx *c = find_cmd_ctx(ctx, ack->request_id);

		if (c == NULL) {
			LOG_WRN("DATA_ACK for unknown command %08X", ack->request_id);
			return true;
		}
		/* ACK received, extend timeout */
		LOG_DBG("ACK received for %08X", ack->request_id);
		k_timer_start(&c->timeout, c->rsp_timeout, K_FOREVER);
		k_sem_give(&c->ack);
	} else if (meta->type == INFUSE_RPC_RSP) {
		struct infuse_rpc_rsp_header *rsp_header = (void *)buf->data;

		LOG_DBG("Finalising request %08X", rsp_header->request_id);
		run_callback(ctx, buf, rsp_header->command_id, rsp_header->request_id);
	}
	return true;
}

void rpc_client_init(struct rpc_client_ctx *ctx, const struct device *dev,
		     union epacket_interface_address address)
{
	ctx->interface = dev;
	ctx->address = address;
	ctx->interface_cb.interface_state = NULL;
	ctx->interface_cb.tx_failure = NULL;
	ctx->interface_cb.packet_received = packet_received;
	ctx->interface_cb.user_ctx = ctx;
	ctx->request_id = sys_rand32_get();
	k_sem_init(&ctx->cmd_ctx_sem, ARRAY_SIZE(ctx->cmd_ctx), ARRAY_SIZE(ctx->cmd_ctx));
	memset(ctx->cmd_ctx, 0x00, sizeof(ctx->cmd_ctx));

	epacket_register_callback(dev, &ctx->interface_cb);
}

int rpc_client_update_response_timeout(struct rpc_client_ctx *ctx, uint32_t request_id,
				       k_timeout_t timeout)
{
	struct rpc_client_cmd_ctx *c = find_cmd_ctx(ctx, request_id);

	if (c == NULL) {
		return -EINVAL;
	}
	/* Update stored value */
	c->rsp_timeout = timeout;
	/* Restart timer with new timeout */
	k_timer_start(&c->timeout, c->rsp_timeout, K_FOREVER);
	return 0;
}

int rpc_client_command_queue(struct rpc_client_ctx *ctx, enum rpc_builtin_id cmd, void *req_params,
			     size_t req_params_len, rpc_client_rsp_fn cb, void *user_data,
			     k_timeout_t ctx_timeout, k_timeout_t response_timeout)
{
	struct infuse_rpc_req_header *req_header = req_params;
	struct rpc_client_cmd_ctx *c;
	uint8_t ctx_idx = UINT8_MAX;
	struct net_buf *cmd_buf;

	__ASSERT_NO_MSG(ctx->interface != NULL);

	/* Invalid input parameters */
	if (K_TIMEOUT_EQ(response_timeout, K_NO_WAIT) || (req_params == NULL) || (cb == NULL)) {
		return -EINVAL;
	}

	/* Wait for free context buffer */
	if (k_sem_take(&ctx->cmd_ctx_sem, ctx_timeout) != 0) {
		return -EAGAIN;
	}

	/* Find free context buffer */
	for (int i = 0; i < ARRAY_SIZE(ctx->cmd_ctx); i++) {
		if (ctx->cmd_ctx[i].request_id == 0) {
			ctx_idx = i;
			break;
		}
	}
	__ASSERT_NO_MSG(ctx_idx != UINT8_MAX);

	/* Increment context ID */
	ctx->request_id += 1;
	if (ctx->request_id == 0) {
		ctx->request_id += 1;
	}

	/* Allocate buffer for command */
	cmd_buf = epacket_alloc_tx_for_interface(ctx->interface, K_FOREVER);
	__ASSERT_NO_MSG(cmd_buf != NULL);

	LOG_DBG("Command %d (request %08X, idx %d)", cmd, ctx->request_id, ctx_idx);

	/* Store command context */
	c = &ctx->cmd_ctx[ctx_idx];
	k_timer_init(&c->timeout, command_timeout, NULL);
	k_sem_init(&c->ack, 0, UINT32_MAX);
	c->timeout.user_data = ctx;
	c->cb = cb;
	c->user_data = user_data;
	c->request_id = ctx->request_id;
	c->command_id = cmd;
	c->rsp_timeout = response_timeout;

	/* Command header */
	req_header->command_id = cmd;
	req_header->request_id = ctx->request_id;

	/* Command payload */
	net_buf_add_mem(cmd_buf, req_params, req_params_len);

	/* Send command */
	epacket_set_tx_metadata(cmd_buf, EPACKET_AUTH_NETWORK, 0x00, INFUSE_RPC_CMD, ctx->address);
	epacket_queue(ctx->interface, cmd_buf);

	/* Start the timeout timer */
	k_timer_start(&c->timeout, response_timeout, K_FOREVER);
	return 0;
}

int rpc_client_ack_wait(struct rpc_client_ctx *ctx, uint32_t request_id, k_timeout_t timeout)
{
	struct rpc_client_cmd_ctx *c = find_cmd_ctx(ctx, request_id);

	if (c == NULL) {
		return -EINVAL;
	}
	return k_sem_take(&c->ack, timeout);
}

int rpc_client_data_queue(struct rpc_client_ctx *ctx, uint32_t request_id, uint32_t offset,
			  const void *data, size_t data_len)
{
	struct rpc_client_cmd_ctx *c = find_cmd_ctx(ctx, request_id);
	struct infuse_rpc_data *header;
	const uint8_t *bytes = data;
	struct net_buf *data_buf;
	size_t add, tail, extra;
	k_ticks_t limit_tx = k_uptime_ticks();

	if (c == NULL) {
		LOG_WRN("Invalid request %08X", request_id);
		return -EINVAL;
	}

	/* Offsets must be word aligned */
	if (offset % sizeof(uint32_t) != 0) {
		return -EINVAL;
	}

	add = 0;
	while (data_len) {
		/* Offsets must be word aligned */
		__ASSERT_NO_MSG((offset % sizeof(uint32_t)) == 0);

		/* Respect any rate-limiting requests from the receiving device */
		epacket_rate_limit_tx(&limit_tx, add);

		/* Allocate buffer for command */
		data_buf = epacket_alloc_tx_for_interface(ctx->interface, K_FOREVER);
		__ASSERT_NO_MSG(data_buf != NULL);

		/* Data header */
		header = net_buf_add(data_buf, sizeof(*header));
		header->request_id = request_id;
		header->offset = offset;

		/* Limit payload to interface size */
		tail = net_buf_tailroom(data_buf);
		/* Chunks should be word aligned */
		extra = tail % sizeof(uint32_t);
		if (extra) {
			tail -= extra;
		}
		add = MIN(tail, data_len);

		/* Data payload */
		net_buf_add_mem(data_buf, bytes, add);

		/* Send data packet */
		epacket_set_tx_metadata(data_buf, EPACKET_AUTH_NETWORK, 0x00, INFUSE_RPC_DATA,
					ctx->address);
		epacket_queue(ctx->interface, data_buf);

		/* Update state */
		bytes += add;
		offset += add;
		data_len -= add;
	}
	return 0;
}

struct sync_ctx {
	struct k_sem done;
	struct net_buf *rsp;
};

static void client_sync_handler(const struct net_buf *buf, void *user_data)
{
	struct sync_ctx *sync = user_data;
	struct net_buf *user = (void *)buf;

	if (buf != NULL) {
		user = net_buf_ref(user);
	}
	sync->rsp = user;
	k_sem_give(&sync->done);
}

int rpc_client_command_sync(struct rpc_client_ctx *ctx, enum rpc_builtin_id cmd, void *req_params,
			    size_t req_params_len, k_timeout_t ctx_timeout,
			    k_timeout_t response_timeout, struct net_buf **rsp)
{
	struct sync_ctx sync;
	int rc;

	k_sem_init(&sync.done, 0, 1);

	/* Queue command */
	rc = rpc_client_command_queue(ctx, cmd, req_params, req_params_len, client_sync_handler,
				      &sync, ctx_timeout, response_timeout);
	if (rc != 0) {
		return rc;
	}

	/* Wait for response */
	k_sem_take(&sync.done, K_FOREVER);
	*rsp = sync.rsp;
	return sync.rsp == NULL ? -ETIMEDOUT : 0;
}

void rpc_client_cleanup(struct rpc_client_ctx *ctx)
{
	/* Unregister from interface */
	epacket_unregister_callback(ctx->interface, &ctx->interface_cb);
	ctx->interface = NULL;

	/* Cleanup any pending commands */
	for (int i = 0; i < ARRAY_SIZE(ctx->cmd_ctx); i++) {
		struct rpc_client_cmd_ctx *c = &ctx->cmd_ctx[i];

		if (c->request_id == 0) {
			continue;
		}
		LOG_DBG("Detaching request %08X", c->request_id);

		/* Terminate timeout timer */
		k_timer_stop(&c->timeout);

		/* Run the callback */
		c->cb(NULL, c->user_data);
	}
}
