/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/logging/log.h>

#include <infuse/gnss/ubx/protocol.h>
#include <infuse/gnss/ubx/modem.h>

LOG_MODULE_REGISTER(ubx_modem, CONFIG_GNSS_UBX_MODEM_LOG_LEVEL);

/* Frame header + 16bit checksum */
#define UBX_FRAME_OVERHEAD (sizeof(struct ubx_frame) + sizeof(uint16_t))
BUILD_ASSERT(UBX_FRAME_OVERHEAD == 8);

static void ubx_modem_pipe_callback(struct modem_pipe *pipe, enum modem_pipe_event event,
				    void *user_data)
{
	struct ubx_modem_data *modem = user_data;

	switch (event) {
	case MODEM_PIPE_EVENT_RECEIVE_READY:
		k_work_submit(&modem->fifo_read_worker);
		break;
	case MODEM_PIPE_EVENT_TRANSMIT_IDLE:
		k_poll_signal_raise(&modem->tx_done, 0);
		break;
	default:
		break;
	}
}

static void ubx_msg_handle_ack(struct ubx_modem_data *modem, struct ubx_frame *frame,
			       uint16_t payload_len)
{
	struct ubx_message_handler_ctx *to_run[CONFIG_GNSS_UBX_MODEM_MAX_CALLBACKS] = {0};
	struct ubx_message_handler_ctx *curr;
	struct ubx_message_handler_ctx *tmp;
	const void *callback_ptr;
	uint16_t callback_len;
	int rc;

	/* ACK-ACK and ACK-NAK have the same payload structures */
	const struct ubx_msg_id_ack_ack *ack = (const void *)frame->payload_and_checksum;

	/* Iterate over all one shot handlers */
	k_sem_take(&modem->handlers_sem, K_FOREVER);
	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&modem->handlers, curr, tmp, _node) {
		/* Only consider handlers expecting an ACK */
		if (!(curr->flags & (UBX_HANDLING_ACK | UBX_HANDLING_RSP_ACK))) {
			continue;
		}
		/* Check message that was acked against handler */
		if ((ack->message_class == curr->message_class) &&
		    (ack->message_id == curr->message_id)) {
			/* Remove from handler list */
			sys_slist_find_and_remove(&modem->handlers, &curr->_node);
			/* Store the one-shot handler that should be run */
			to_run[0] = curr;
			break;
		}
	}
	k_sem_give(&modem->handlers_sem);

	if (to_run[0]) {
		/* If UBX_HANDLING_RSP_ACK, handler was for the main response */
		ubx_message_handler_t cb = curr->flags & UBX_HANDLING_RSP_ACK
						   ? ubx_modem_ack_handler
						   : curr->message_cb;

		/* Desired callback pointer */
		callback_ptr = frame->payload_and_checksum;
		callback_len = payload_len;
		if (curr->flags & UBX_HANDLING_RAW_FRAME_CB) {
			callback_ptr = frame;
			callback_len += UBX_FRAME_OVERHEAD;
		}

		/* Run callback */
		rc = cb(frame->message_class, frame->message_id, callback_ptr, callback_len,
			curr->user_data);
		/* Raise signal if provided */
		if (curr->signal) {
			k_poll_signal_raise(curr->signal, rc);
		}
	} else {
		if (!IS_ENABLED(CONFIG_GNSS_UBX_RAW_FRAME_HANDLER)) {
			LOG_WRN("Unhandled ACK for %02x:%02x", ack->message_class, ack->message_id);
		}
	}
}

static void ubx_msg_handle_non_ack(struct ubx_modem_data *modem, struct ubx_frame *frame,
				   uint16_t payload_len)
{
	struct ubx_message_handler_ctx *to_run[CONFIG_GNSS_UBX_MODEM_MAX_CALLBACKS] = {0};
	struct ubx_message_handler_ctx *curr;
	struct ubx_message_handler_ctx *tmp;
	const void *callback_ptr;
	uint16_t callback_len;
	int num_to_run = 0;
	bool notify;
	int rc;

	/* Iterate over all pending message callbacks */
	k_sem_take(&modem->handlers_sem, K_FOREVER);
	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&modem->handlers, curr, tmp, _node) {
		/* Notify if:
		 *   Handler class == UBX_MSG_CLASS_WILDCARD
		 * or:
		 *   Handler class == frame->class
		 *   and
		 *     Handler ID == UBX_MSG_ID_WILDCARD
		 *   or:
		 *     Handler ID == frame->id
		 */
		notify = (curr->message_class == UBX_MSG_CLASS_WILDCARD) ||
			 ((frame->message_class == curr->message_class) &&
			  ((frame->message_id == UBX_MSG_ID_WILDCARD) ||
			   (frame->message_id == curr->message_id)));

		if (notify) {
			/* Remove from handler list if a single response */
			if (curr->flags & UBX_HANDLING_RSP) {
				sys_slist_find_and_remove(&modem->handlers, &curr->_node);
			}
			/* Cache the contexts to run outside of the list lock */
			if (num_to_run < ARRAY_SIZE(to_run)) {
				to_run[num_to_run++] = curr;
			} else {
				LOG_ERR("Too many handlers!");
			}
		}
	}
	k_sem_give(&modem->handlers_sem);

	/* Run the saved contexts */
	for (int i = 0; i < num_to_run; i++) {
		/* Desired callback pointer */
		callback_ptr = frame->payload_and_checksum;
		callback_len = payload_len;
		if (to_run[i]->flags & UBX_HANDLING_RAW_FRAME_CB) {
			callback_ptr = frame;
			callback_len += UBX_FRAME_OVERHEAD;
		}
		/* Run callback */
		rc = to_run[i]->message_cb(frame->message_class, frame->message_id, callback_ptr,
					   callback_len, to_run[i]->user_data);
		/* Raise signal if provided and no ACK/NAK is expected */
		if (to_run[i]->signal && (to_run[i]->flags & UBX_HANDLING_RSP)) {
			k_poll_signal_raise(to_run[i]->signal, rc);
		}
	}
}

static void ubx_msg_handle(struct ubx_modem_data *modem, struct ubx_frame *frame,
			   uint16_t payload_len)
{
#ifdef CONFIG_GNSS_UBX_RAW_FRAME_HANDLER
	{
		uint16_t total_len = sizeof(*frame) + payload_len + sizeof(uint16_t);

		ubx_modem_raw_frame_handler(frame, total_len);
	}
#endif /* CONFIG_GNSS_UBX_RAW_FRAME_HANDLER*/

	if (frame->message_class == UBX_MSG_CLASS_ACK) {
		ubx_msg_handle_ack(modem, frame, payload_len);
	} else {
		ubx_msg_handle_non_ack(modem, frame, payload_len);
	}
}

static bool fifo_read_to_sync(struct ubx_modem_data *modem, struct ubx_frame *frame)
{
	/* Pull single bytes until sync bytes found */
	while (modem->rx_buffer_pending < 2) {
		if (modem_pipe_receive(modem->pipe, modem->rx_buffer + modem->rx_buffer_pending,
				       1) <= 0) {
			return true;
		}

		if ((modem->rx_buffer_pending) == 0 &&
		    (frame->preamble_sync_char_1 != UBX_PREAMBLE_SYNC_CHAR_1)) {
			continue;
		}
		if ((modem->rx_buffer_pending) == 1 &&
		    (frame->preamble_sync_char_2 != UBX_PREAMBLE_SYNC_CHAR_2)) {
			modem->rx_buffer_pending = 0;
			continue;
		}
		modem->rx_buffer_pending++;
	}
	return false;
}

static void fifo_read_runner(struct k_work *work)
{
	struct ubx_modem_data *modem = CONTAINER_OF(work, struct ubx_modem_data, fifo_read_worker);
	struct ubx_frame *frame = (void *)(modem->rx_buffer);
	uint16_t payload_len;
	uint16_t message_len;
	uint16_t to_read;
	int received;

	while (true) {
		/* Read until we see SYNC bytes */
		if (fifo_read_to_sync(modem, frame)) {
			return;
		}

		/* Pull remainder of frame header */
		if (modem->rx_buffer_pending < sizeof(*frame)) {
			to_read = sizeof(*frame) - modem->rx_buffer_pending;
			received = modem_pipe_receive(
				modem->pipe, modem->rx_buffer + modem->rx_buffer_pending, to_read);
			if (received < 0) {
				return;
			}
			modem->rx_buffer_pending += received;
			if (received != to_read) {
				return;
			}
		}

		payload_len = (((uint16_t)frame->payload_size_high) << 8) | frame->payload_size_low;
		message_len = sizeof(*frame) + payload_len + sizeof(uint16_t);

		/* Validate complete message can fit */
		if (message_len >= sizeof(modem->rx_buffer)) {
			LOG_ERR("RX MSG: %02X:%02X too large (%d)", frame->message_class,
				frame->message_id, message_len);
			/* Consume any part of the message still in queue */
			modem_pipe_receive(modem->pipe, modem->rx_buffer, sizeof(modem->rx_buffer));
			/* Reset stored buffer and exit */
			modem->rx_buffer_pending = 0;
			return;
		}

		/* Attempt to pull remaining message payload */
		to_read = message_len - modem->rx_buffer_pending;
		received = modem_pipe_receive(modem->pipe,
					      modem->rx_buffer + modem->rx_buffer_pending, to_read);
		if (received < 0) {
			return;
		}
		modem->rx_buffer_pending += received;
		if (received != to_read) {
			LOG_DBG("Waiting on %d", to_read - received);
			return;
		}

		LOG_DBG("RX MSG: CLS=0x%02x ID=0x%02x LEN=%d", frame->message_class,
			frame->message_id, payload_len);
		LOG_HEXDUMP_DBG(frame->payload_and_checksum, payload_len, "Payload");

		/* Process frame */
		ubx_msg_handle(modem, frame, payload_len);

		/* Complete */
		modem->rx_buffer_pending = 0;
	}
}

void ubx_modem_init(struct ubx_modem_data *modem, struct modem_pipe *pipe)
{
	modem->pipe = pipe;
	sys_slist_init(&modem->handlers);
	k_sem_init(&modem->handlers_sem, 1, 1);
	modem_pipe_attach(modem->pipe, ubx_modem_pipe_callback, modem);
	k_poll_signal_init(&modem->tx_done);
	k_work_init(&modem->fifo_read_worker, fifo_read_runner);
}

void ubx_modem_software_standby(struct ubx_modem_data *modem)
{
	struct ubx_message_handler_ctx *prev = NULL;
	struct ubx_message_handler_ctx *curr;
	struct ubx_message_handler_ctx *tmp;

	/** Purge any callbacks expecting a response */
	k_sem_take(&modem->handlers_sem, K_FOREVER);
	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&modem->handlers, curr, tmp, _node) {
		if (curr->flags) {
			/* Notify waiter */
			if (curr->signal) {
				k_poll_signal_raise(curr->signal, -EIO);
			}
			/* Remove from list */
			sys_slist_remove(&modem->handlers, &prev->_node, &curr->_node);
			continue;
		}
		prev = curr;
	}
	k_sem_give(&modem->handlers_sem);
}

void ubx_modem_msg_subscribe(struct ubx_modem_data *modem,
			     struct ubx_message_handler_ctx *handler_ctx)
{
	k_sem_take(&modem->handlers_sem, K_FOREVER);
	sys_slist_append(&modem->handlers, &handler_ctx->_node);
	k_sem_give(&modem->handlers_sem);
}

void ubx_modem_msg_unsubscribe(struct ubx_modem_data *modem,
			       struct ubx_message_handler_ctx *handler_ctx)
{
	k_sem_take(&modem->handlers_sem, K_FOREVER);
	sys_slist_find_and_remove(&modem->handlers, &handler_ctx->_node);
	k_sem_give(&modem->handlers_sem);
}

int ubx_modem_send_async(struct ubx_modem_data *modem, struct net_buf_simple *buf,
			 struct ubx_message_handler_ctx *handler_ctx, bool wait_tx)
{
	struct k_poll_event events[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY,
					 &modem->tx_done),
	};
	int rc;

	if (handler_ctx == NULL) {
		__ASSERT_NO_MSG(wait_tx == true);
		k_poll_signal_reset(&modem->tx_done);
	} else {
		/* Push handler onto queue */
		k_sem_take(&modem->handlers_sem, K_FOREVER);
		sys_slist_append(&modem->handlers, &handler_ctx->_node);
		k_sem_give(&modem->handlers_sem);
	}
	/* Transmit message */
	rc = modem_pipe_transmit(modem->pipe, buf->data, buf->len);
	/* If there is no callback structure, wait for TX to complete */
	if ((rc >= 0) && wait_tx) {
		k_poll(events, ARRAY_SIZE(events), K_FOREVER);
	}
	return rc;
}

int ubx_modem_send_sync(struct ubx_modem_data *modem, struct net_buf_simple *buf, uint8_t flags,
			ubx_message_handler_t handler, void *user_data, k_timeout_t timeout)
{
	struct ubx_frame *frame = (void *)buf->data;
	struct k_poll_signal sig;
	struct ubx_message_handler_ctx handler_pkg = {
		.message_class = frame->message_class,
		.message_id = frame->message_id,
		.message_cb = handler,
		.user_data = user_data,
		.signal = &sig,
		.flags = flags,
	};
	struct k_poll_event events[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &sig),
	};
	int signaled;
	int rc;

	k_poll_signal_init(&sig);

	/* Trigger message send */
	rc = ubx_modem_send_async(modem, buf, &handler_pkg, false);
	if (rc < 0) {
		return rc;
	}
	/* Wait for response */
	(void)k_poll(events, ARRAY_SIZE(events), timeout);
	/* Check and return result */
	k_poll_signal_check(&sig, &signaled, &rc);
	if (!signaled) {
		/* Remove from handlers */
		ubx_modem_msg_unsubscribe(modem, &handler_pkg);
		return -ETIMEDOUT;
	}
	return rc;
}

int ubx_modem_ack_handler(uint8_t message_class, uint8_t message_id, const void *payload,
			  size_t payload_len, void *user_data)
{
	/* ACK-ACK and ACK-NAK have the same payload structures */
	const struct ubx_msg_id_ack_ack *ack = payload;

	__ASSERT_NO_MSG(message_class == UBX_MSG_CLASS_ACK);
	__ASSERT_NO_MSG(payload_len == sizeof(struct ubx_msg_id_ack_ack));

	LOG_DBG("%s for msg %02x:%02X", message_id == UBX_MSG_ID_ACK_ACK ? "ACK" : "NAK",
		ack->message_class, ack->message_id);
	return message_id == UBX_MSG_ID_ACK_ACK ? 0 : -EINVAL;
}

int ubx_modem_send_sync_acked(struct ubx_modem_data *modem, struct net_buf_simple *buf,
			      k_timeout_t timeout)
{
	return ubx_modem_send_sync(modem, buf, UBX_HANDLING_ACK, ubx_modem_ack_handler, NULL,
				   timeout);
}

static int ubx_sync_poll_internal(struct ubx_modem_data *modem, uint8_t message_class,
				  uint8_t message_id, uint8_t flags, ubx_message_handler_t handler,
				  void *user_data, k_timeout_t timeout)
{
	NET_BUF_SIMPLE_DEFINE(poll_req, 16);

	/* Poll requests are just 0 byte messages with the given class and id */
	ubx_msg_prepare(&poll_req, message_class, message_id);
	ubx_msg_finalise(&poll_req);

	/* CFG messages always generate an ACK as well */
	flags |= (message_class == UBX_MSG_CLASS_CFG) ? UBX_HANDLING_RSP_ACK : UBX_HANDLING_RSP;

	return ubx_modem_send_sync(modem, &poll_req, flags, handler, user_data, timeout);
}

int ubx_modem_send_sync_poll(struct ubx_modem_data *modem, uint8_t message_class,
			     uint8_t message_id, ubx_message_handler_t handler, void *user_data,
			     k_timeout_t timeout)
{
	return ubx_sync_poll_internal(modem, message_class, message_id, 0, handler, user_data,
				      timeout);
}

int ubx_modem_send_sync_raw_poll(struct ubx_modem_data *modem, uint8_t message_class,
				 uint8_t message_id, ubx_message_handler_t handler, void *user_data,
				 k_timeout_t timeout)
{
	return ubx_sync_poll_internal(modem, message_class, message_id, UBX_HANDLING_RAW_FRAME_CB,
				      handler, user_data, timeout);
}

int ubx_modem_send_async_poll(struct ubx_modem_data *modem, uint8_t message_class,
			      uint8_t message_id, uint8_t buf[8],
			      struct ubx_message_handler_ctx *handler_ctx)
{
	struct net_buf_simple poll_req;

	net_buf_simple_init_with_data(&poll_req, buf, 8);

	/* Poll requests are just 0 byte messages with the given class and id */
	ubx_msg_prepare(&poll_req, message_class, message_id);
	ubx_msg_finalise(&poll_req);

	handler_ctx->flags = UBX_HANDLING_RSP;
	handler_ctx->message_class = message_class;
	handler_ctx->message_id = message_id;

	return ubx_modem_send_async(modem, &poll_req, handler_ctx, false);
}
