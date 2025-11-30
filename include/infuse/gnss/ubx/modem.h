/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_DRIVERS_GNSS_UBX_MODEM_H_
#define INFUSE_SDK_DRIVERS_GNSS_UBX_MODEM_H_

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/modem/pipe.h>
#include <zephyr/sys/ring_buffer.h>

#include <infuse/gnss/ubx/protocol.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup ubx_modem_apis UBX Modem Communications
 * @{
 */

/**
 * @brief UBX Message handler callback
 *
 * @param message_class UBX message class
 * @param message_id UBX message ID
 * @param payload Pointer to message payload
 * @param payload_len Length of message payload
 * @param user_data Arbitrary user data
 */
typedef int (*ubx_message_handler_t)(uint8_t message_class, uint8_t message_id, const void *payload,
				     size_t payload_len, void *user_data);

#define UBX_MSG_CLASS_WILDCARD 0x00
#define UBX_MSG_ID_WILDCARD    0x00

enum ubx_message_handler_flags {
	/* Message results in an ACK/NAK only */
	UBX_HANDLING_ACK = BIT(0),
	/* Message results in a direct response only */
	UBX_HANDLING_RSP = BIT(1),
	/* Message results in a response AND an ACK/NAK */
	UBX_HANDLING_RSP_ACK = BIT(2),
};

/**
 * @brief Context around a @ref ubx_message_handler_t
 */
struct ubx_message_handler_ctx {
	/** Iteration structure for callback lists */
	sys_snode_t _node;
	/** Signal raise after callback run */
	struct k_poll_signal *signal;
	/** Callback to run */
	ubx_message_handler_t message_cb;
	/** Arbitrary user data, provided to @a message_cb */
	void *user_data;
	/** Message class callback should be run on (or @ref UBX_MSG_CLASS_WILDCARD) */
	uint8_t message_class;
	/** Message ID callback should be run on (or @ref UBX_MSG_ID_WILDCARD) */
	uint8_t message_id;
	/** Flags for callback (@ref ubx_message_handler_flags) */
	uint8_t flags;
};

/**
 * @brief UBX modem state
 */
struct ubx_modem_data {
	/* Pipe for communications with underlying modem */
	struct modem_pipe *pipe;
	/* Worker to read data */
	struct k_work fifo_read_worker;
	/* Notification that TX has completed */
	struct k_poll_signal tx_done;
	/* List of message handlers */
	sys_slist_t handlers;
	/* Thread protection for handlers list */
	struct k_sem handlers_sem;
	/* Data buffer to read bytes into */
	uint8_t rx_buffer[CONFIG_GNSS_U_BLOX_MAX_MSG_SIZE];
	/* Bytes pending in rx_buffer */
	uint16_t rx_buffer_pending;
};

/**
 * @brief Get modem data structure from device
 *
 * @param dev Modem device
 *
 * @return Pointer to underlying modem state
 */
struct ubx_modem_data *ubx_modem_data_get(const struct device *dev);

/**
 * @brief Explicitly trigger a poll of the modem data FIFO
 *
 * Only required if the TX READY pin is not operational.
 *
 * @param dev Modem device
 */
void ubx_modem_fifo_poll(const struct device *dev);

/**
 * @brief Control the EXTINT pin state
 *
 * @param dev Modem device to control EXTINT pin state for
 * @param high True to drive pin high, low otherwise
 */
void ubx_modem_extint_control(const struct device *dev, bool high);

/**
 * @brief Initialise UBX modem handler
 *
 * @param modem Modem data structure
 * @param pipe Pipe to underlying modem hardware
 */
void ubx_modem_init(struct ubx_modem_data *modem, struct modem_pipe *pipe);

/**
 * @brief Reset communications with the UBX modem
 *
 * Depends on the modem being in `PM_DEVICE_STATE_SUSPENDED`.
 *
 * @param dev Modem device to reset communications for
 *
 * @retval 0 On success
 * @retval -errno Negative error code otherwise
 */
int ubx_modem_comms_reset(const struct device *dev);

/**
 * @brief Notify UBX modem that hardware is in standby
 *
 * @param modem Modem data structure
 */
void ubx_modem_software_standby(struct ubx_modem_data *modem);

/**
 * @brief Subscribe to messages from the UBX modem
 *
 * @param modem Modem data structure
 * @param handler_ctx Handler context structure
 */
void ubx_modem_msg_subscribe(struct ubx_modem_data *modem,
			     struct ubx_message_handler_ctx *handler_ctx);

/**
 * @brief Unsubscribe from messages from the UBX modem
 *
 * @param modem Modem data structure
 * @param handler_ctx Handler context structure
 */
void ubx_modem_msg_unsubscribe(struct ubx_modem_data *modem,
			       struct ubx_message_handler_ctx *handler_ctx);

/**
 * @brief Common ACK handler for messages
 *
 * @param message_class Expected to be @ref UBX_MSG_CLASS_ACK
 * @param message_id Expected to be either @ref UBX_MSG_ID_ACK_ACK or @ref UBX_MSG_ID_ACK_NAK
 * @param payload ACK payload
 * @param payload_len ACK payload length
 * @param user_data Unused
 *
 * @retval 0 If message_id == @ref UBX_MSG_ID_ACK_ACK
 * @retval -EINVAL Otherwise
 */
int ubx_modem_ack_handler(uint8_t message_class, uint8_t message_id, const void *payload,
			  size_t payload_len, void *user_data);

/**
 * @brief Send a message to the modem without waiting for a response
 *
 * @note @a handler_ctx MUST remain valid memory until the callback has executed
 *       if `handler_ctx->signal == NULL`, or until after the callback runs otherwise.
 *
 * @param modem Modem data structure
 * @param buf Complete UBX message
 * @param handler_ctx Handler context object
 * @param wait_tx Wait for transmission to finish before returning
 *
 * @retval 0 if TX was queued
 * @retval -errno Negative error code otherwise
 */
int ubx_modem_send_async(struct ubx_modem_data *modem, struct net_buf_simple *buf,
			 struct ubx_message_handler_ctx *handler_ctx, bool wait_tx);

/**
 * @brief Send a message to the modem and wait for the response
 *
 * @note This is unlikely to be the function you want, see @ref ubx_modem_send_sync_acked and
 *       @ref ubx_modem_send_sync_poll
 *
 * @param modem Modem data structure
 * @param buf Complete UBX message
 * @param flags Flags for callback (@ref ubx_message_handler_flags)
 * @param handler Callback function run on message reception
 * @param user_data Arbitrary user data provided to @a handler
 * @param timeout Duration to wait for response
 *
 * @retval rc Returned code from @a handler on success
 * @retval -ETIMEDOUT On response timeout
 * @retval -errno Negative error code otherwise
 */
int ubx_modem_send_sync(struct ubx_modem_data *modem, struct net_buf_simple *buf, uint8_t flags,
			ubx_message_handler_t handler, void *user_data, k_timeout_t timeout);

/**
 * @brief Send a message to the modem and wait for the UBX_MSG_CLASS_ACK response
 *
 * @param modem Modem data structure
 * @param buf Complete UBX message
 * @param timeout Duration to wait for response
 *
 * @retval 0 If modem responded with UBX_MSG_ID_ACK_ACK
 * @retval -EINVAL If modem responded with UBX_MSG_ID_ACK_NAK
 * @retval -ETIMEDOUT If modem did not respond within timeout
 * @retval -errno Negative error code otherwise
 */
int ubx_modem_send_sync_acked(struct ubx_modem_data *modem, struct net_buf_simple *buf,
			      k_timeout_t timeout);

/**
 * @brief Request a poll response from the modem and wait for the response
 *
 * @param modem Modem data structure
 * @param message_class UBX message class to poll
 * @param message_id UBX message ID to poll
 * @param handler Callback to handle the poll response
 * @param user_data Arbitrary user data provided to @a handler
 * @param timeout Duration to wait for response
 *
 * @retval rc Returned code from @a handler on success
 * @retval -ETIMEDOUT On response timeout
 * @retval -errno Negative error code otherwise
 */
int ubx_modem_send_sync_poll(struct ubx_modem_data *modem, uint8_t message_class,
			     uint8_t message_id, ubx_message_handler_t handler, void *user_data,
			     k_timeout_t timeout);

/**
 * @brief Request a poll response from the modem
 *
 * It is expected that @a handler_ctx has its @a message_cb and @a user_data
 * fields populated before calling this function.
 *
 * @param modem Modem data structure
 * @param message_class UBX message class to poll
 * @param message_id UBX message ID to poll
 * @param buf Static buffer of at least 8 bytes for request
 * @param handler_ctx Handler context object
 *
 * @retval rc Returned code from @a handler on success
 * @retval -ETIMEDOUT On response timeout
 * @retval -errno Negative error code otherwise
 */
int ubx_modem_send_async_poll(struct ubx_modem_data *modem, uint8_t message_class,
			      uint8_t message_id, uint8_t buf[8],
			      struct ubx_message_handler_ctx *handler_ctx);

#ifdef CONFIG_GNSS_UBX_RAW_FRAME_HANDLER

/**
 * @brief Function prototype for raw UBX frame handler
 *
 * When CONFIG_GNSS_UBX_RAW_FRAME_HANDLER is enabled, the application must supply an
 * implementation of this function.
 *
 * @param frame Pointer to the receieved UBX frame
 * @param total_len Total length of the UBX frame (including headers + checksum)
 */
void ubx_modem_raw_frame_handler(struct ubx_frame *frame, uint16_t total_len);

#endif

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_DRIVERS_GNSS_UBX_MODEM_H_ */
