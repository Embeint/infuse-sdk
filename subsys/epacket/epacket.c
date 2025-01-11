/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/buf.h>

#include <infuse/drivers/watchdog.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/interface/epacket_serial.h>

#ifdef CONFIG_INFUSE_SECURITY
#include <infuse/security.h>
#endif

#include "interfaces/epacket_internal.h"

NET_BUF_POOL_DEFINE(epacket_scratch, 1, CONFIG_EPACKET_PACKET_SIZE_MAX, 0, NULL);
NET_BUF_POOL_DEFINE(epacket_pool_tx, CONFIG_EPACKET_BUFFERS_TX, CONFIG_EPACKET_PACKET_SIZE_MAX,
		    sizeof(struct epacket_tx_metadata), NULL);
NET_BUF_POOL_DEFINE(epacket_pool_rx, CONFIG_EPACKET_BUFFERS_RX, CONFIG_EPACKET_PACKET_SIZE_MAX,
		    sizeof(struct epacket_rx_metadata), NULL);

K_THREAD_STACK_DEFINE(epacket_stack_area, 2048);
static struct k_thread epacket_process_thread;
COND_CODE_0(CONFIG_ZTEST, (static), ())
k_tid_t epacket_processor_thread;

static K_FIFO_DEFINE(epacket_rx_queue);
static K_FIFO_DEFINE(epacket_tx_queue);
static const struct device *tx_device[CONFIG_EPACKET_BUFFERS_TX];
static k_timeout_t loop_period = K_FOREVER;
static int wdog_channel;

#ifdef CONFIG_EPACKET_INTERFACE_BT_ADV
static struct k_poll_signal bt_adv_signal_send_next;
#endif /* CONFIG_EPACKET_INTERFACE_BT_ADV */

LOG_MODULE_REGISTER(epacket, CONFIG_EPACKET_LOG_LEVEL);

static void epacket_receive_timeout(struct k_work *work)
{
	struct k_work_delayable *delayable = k_work_delayable_from_work(work);
	struct epacket_interface_common_data *data =
		CONTAINER_OF(delayable, struct epacket_interface_common_data, receive_timeout);
	const struct epacket_interface_api *api = data->dev->api;

	/* Disable reception on the interface */
	LOG_DBG("Receive on %s expired", data->dev->name);
	api->receive_ctrl(data->dev, false);
}

void epacket_interface_common_init(const struct device *dev)
{
	struct epacket_interface_common_data *data = dev->data;

	data->dev = dev;
	data->receive_handler = epacket_default_receive_handler;
	k_work_init_delayable(&data->receive_timeout, epacket_receive_timeout);
	sys_slist_init(&data->callback_list);
}

struct net_buf *epacket_encryption_scratch(void)
{
	return net_buf_alloc(&epacket_scratch, K_FOREVER);
}

struct net_buf *epacket_alloc_tx(k_timeout_t timeout)
{
	return net_buf_alloc(&epacket_pool_tx, timeout);
}

struct net_buf *epacket_alloc_rx(k_timeout_t timeout)
{
	struct net_buf *buf = net_buf_alloc(&epacket_pool_rx, timeout);

	if (buf != NULL) {
		struct epacket_rx_metadata *meta = net_buf_user_data(buf);
		/* Default authorisation state is failure */
		meta->auth = EPACKET_AUTH_FAILURE;
	}
	return buf;
}

#ifdef CONFIG_INFUSE_SECURITY
int epacket_send_key_ids(const struct device *dev, k_timeout_t timeout)
{
	struct net_buf *rsp = epacket_alloc_tx_for_interface(dev, timeout);

	if (rsp) {
		/* Infuse ID and network key ID in header, device key ID in payload */
		epacket_set_tx_metadata(rsp, EPACKET_AUTH_NETWORK, 0, INFUSE_KEY_IDS,
					EPACKET_ADDR_ALL);
		net_buf_add_le24(rsp, infuse_security_device_key_identifier());
		epacket_queue(dev, rsp);
	}
	return rsp == NULL ? -EAGAIN : 0;
}
#endif /* CONFIG_INFUSE_SECURITY */

void epacket_queue(const struct device *dev, struct net_buf *buf)
{
	/* Store transmit device */
	tx_device[net_buf_id(buf)] = dev;

	/* Push packet at processing queue */
	net_buf_put(&epacket_tx_queue, buf);
}

int epacket_receive(const struct device *dev, k_timeout_t timeout)
{
	struct epacket_interface_common_data *data = dev->data;
	const struct epacket_interface_api *api = dev->api;
	int rc;

	/* If not control is available there is nothing to do */
	if (api->receive_ctrl == NULL) {
		return -ENOTSUP;
	}

	/* Enable receiving if the timeout is not immediate */
	if (!K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
		rc = api->receive_ctrl(dev, true);
		if (rc < 0) {
			return rc;
		}
	}

	/* No timeout required */
	if (K_TIMEOUT_EQ(timeout, K_FOREVER)) {
		return 0;
	}

	/* Schedule the receive termination work */
	return k_work_reschedule(&data->receive_timeout, timeout);
}

void epacket_raw_receive_handler(struct net_buf *buf)
{
	/* Push packet at processing queue */
	net_buf_put(&epacket_rx_queue, buf);
}

void epacket_notify_tx_result(const struct device *dev, struct net_buf *buf, int result)
{
	struct epacket_tx_metadata *meta = net_buf_user_data(buf);
	struct epacket_interface_common_data *data = dev->data;
	struct epacket_interface_cb *cb;

	/* Per buffer notification */
	if (meta->tx_done) {
		meta->tx_done(dev, buf, result);
	}

	/* Interface error notification */
	if (result < 0) {
		SYS_SLIST_FOR_EACH_CONTAINER(&data->callback_list, cb, node) {
			if (cb->tx_failure) {
				cb->tx_failure(buf, result, cb->user_ctx);
			}
		}
	}
}

static void epacket_handle_rx(struct net_buf *buf)
{
	struct epacket_interface_common_data *interface_data;
	struct epacket_rx_metadata *metadata = net_buf_user_data(buf);
	const struct epacket_interface_api *api = metadata->interface->api;
	struct epacket_interface_cb *cb, *cbs;
	int rc;

	interface_data = metadata->interface->data;

	LOG_DBG("%s: received %d byte packet (%d dBm)", metadata->interface->name, buf->len,
		metadata->rssi);

#ifdef CONFIG_INFUSE_SECURITY
	static uint32_t prev_key_request;
	uint32_t uptime = k_uptime_seconds();

	/* Key ID request */
	if ((buf->len == 1) && (buf->data[0] == EPACKET_KEY_ID_REQ_MAGIC)) {
		/* Limit responses to one per second to limit the number of packets
		 * an unauthenticated peer can trigger.
		 */
		if (prev_key_request == uptime) {
			LOG_WRN("Too many INFUSE_KEY_IDS requests");
			net_buf_unref(buf);
			return;
		}
		prev_key_request = uptime;

		if (epacket_send_key_ids(metadata->interface, K_NO_WAIT) != 0) {
			LOG_WRN("Unable to respond to key ID request");
		}
		net_buf_unref(buf);
		return;
	}
#endif /* CONFIG_INFUSE_SECURITY */

	/* Payload decoding */
	switch (metadata->interface_id) {
#ifdef CONFIG_EPACKET_INTERFACE_SERIAL
	case EPACKET_INTERFACE_SERIAL:
		rc = epacket_serial_decrypt(buf);
		break;
#endif /* CONFIG_EPACKET_INTERFACE_SERIAL */
#ifdef CONFIG_EPACKET_INTERFACE_UDP
	case EPACKET_INTERFACE_UDP:
		rc = epacket_udp_decrypt(buf);
		break;
#endif /* CONFIG_EPACKET_INTERFACE_UDP */
#ifdef CONFIG_EPACKET_INTERFACE_BT_ADV
	case EPACKET_INTERFACE_BT_ADV:
		rc = epacket_bt_adv_decrypt(buf);
		break;
#endif /* CONFIG_EPACKET_INTERFACE_BT_ADV */
#ifdef CONFIG_EPACKET_INTERFACE_BT_PERIPHERAL
	case EPACKET_INTERFACE_BT_PERIPHERAL:
		rc = epacket_bt_gatt_decrypt(buf);
		break;
#endif /* CONFIG_EPACKET_INTERFACE_BT_PERIPHERAL */
#ifdef CONFIG_EPACKET_INTERFACE_BT_CENTRAL
	case EPACKET_INTERFACE_BT_CENTRAL:
		rc = epacket_bt_gatt_decrypt(buf);
		break;
#endif /* CONFIG_EPACKET_INTERFACE_BT_CENTRAL */
#ifdef CONFIG_EPACKET_INTERFACE_HCI
	case EPACKET_INTERFACE_HCI:
		rc = epacket_hci_decrypt(buf);
		break;
#endif /* CONFIG_EPACKET_INTERFACE_HCI */
#ifdef CONFIG_EPACKET_INTERFACE_DUMMY
	case EPACKET_INTERFACE_DUMMY:
		rc = epacket_dummy_decrypt(buf);
		break;
#endif /* CONFIG_EPACKET_INTERFACE_DUMMY */
	default:
		LOG_WRN("Unknown interface ID %d", metadata->interface_id);
		rc = -1;
	}
	LOG_DBG("Decrypt result: %d", rc);
	if (api->decrypt_result != NULL) {
		/* Notify backend of decryption result */
		api->decrypt_result(metadata->interface, buf, rc);
	}

	/* Run any external interface receive callbacks
	 * (safe as callback may trigger unregistration)
	 */
	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&interface_data->callback_list, cb, cbs, node) {
		if (cb->packet_received) {
			cb->packet_received(buf, rc == 0, cb->user_ctx);
		}
	}

	/* Payload handling */
	interface_data->receive_handler(buf);
}

static void epacket_handle_tx(struct net_buf *buf)
{
	const struct epacket_interface_api *api;
	const struct device *dev;
	size_t pool_max;

	dev = tx_device[net_buf_id(buf)];
	api = dev->api;
	pool_max = net_buf_pool_get(buf->pool_id)->alloc->max_alloc_size;

	/* Reverse any footer reservation that was done at allocation */
	if (buf->size < pool_max) {
		buf->size = pool_max;
	}

	LOG_DBG("%s: TX %d byte packet", dev->name, buf->len);
	/* Run the send function of the interface*/
	api->send(dev, buf);
}

#ifdef CONFIG_EPACKET_INTERFACE_BT_ADV
void epacket_bt_adv_send_next_trigger(void)
{
	k_poll_signal_raise(&bt_adv_signal_send_next, 0);
}
#endif

static void epacket_processor(void *a, void *b, void *c)
{
	struct k_poll_event events[] = {
		K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_FIFO_DATA_AVAILABLE,
						K_POLL_MODE_NOTIFY_ONLY, &epacket_rx_queue, 0),
		K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_FIFO_DATA_AVAILABLE,
						K_POLL_MODE_NOTIFY_ONLY, &epacket_tx_queue, 0),
#ifdef CONFIG_EPACKET_INTERFACE_BT_ADV
		K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY,
						&bt_adv_signal_send_next, 0),
#endif
	};
	struct net_buf *buf;
	int rc;

#ifdef CONFIG_EPACKET_INTERFACE_BT_ADV
	k_poll_signal_init(&bt_adv_signal_send_next);
#endif

	k_thread_name_set(NULL, "epacket_proc");
	infuse_watchdog_thread_register(wdog_channel, _current);
	while (true) {
		rc = k_poll(events, ARRAY_SIZE(events), loop_period);
		infuse_watchdog_feed(wdog_channel);
		if (rc == -EAGAIN) {
			/* Only woke to feed the watchdog */
			continue;
		}

		if (events[0].state == K_POLL_STATE_FIFO_DATA_AVAILABLE) {
			buf = net_buf_get(events[0].fifo, K_NO_WAIT);
			epacket_handle_rx(buf);
			events[0].state = K_POLL_STATE_NOT_READY;
		}

		if (events[1].state == K_POLL_STATE_FIFO_DATA_AVAILABLE) {
			buf = net_buf_get(events[1].fifo, K_NO_WAIT);
			epacket_handle_tx(buf);
			events[1].state = K_POLL_STATE_NOT_READY;
		}

#ifdef CONFIG_EPACKET_INTERFACE_BT_ADV
		if (events[2].state == K_POLL_STATE_SIGNALED) {
			k_poll_signal_reset(&bt_adv_signal_send_next);
			epacket_bt_adv_send_next();
			events[2].state = K_POLL_STATE_NOT_READY;
		}
#endif

		/* Feed watchdog before sleeping again */
		infuse_watchdog_feed(wdog_channel);
	}
}

static int epacket_boot(void)
{
	wdog_channel = IS_ENABLED(CONFIG_EPACKET_INFUSE_WATCHDOG)
			       ? infuse_watchdog_install(&loop_period)
			       : -ENODEV;

	epacket_processor_thread =
		k_thread_create(&epacket_process_thread, epacket_stack_area,
				K_THREAD_STACK_SIZEOF(epacket_stack_area), epacket_processor, NULL,
				NULL, NULL, 0, K_ESSENTIAL, K_NO_WAIT);
	return 0;
}

SYS_INIT(epacket_boot, POST_KERNEL, 0);
