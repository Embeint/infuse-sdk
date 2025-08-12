/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/dns_resolve.h>
#include <zephyr/random/random.h>

#include <infuse/identifiers.h>
#include <infuse/reboot.h>
#include <infuse/time/epoch.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_udp.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/net/dns.h>

#ifdef CONFIG_MEMFAULT_INFUSE_METRICS_EPACKET_UDP
#include <memfault/metrics/metrics.h>
#else
#define MEMFAULT_METRIC_ADD(x, val)
#define MEMFAULT_METRIC_TIMER_START(x)
#define MEMFAULT_METRIC_TIMER_STOP(x)
#endif
#ifdef CONFIG_MEMFAULT_INFUSE_METRICS_SYNC_SUCCESS_EPACKET_UDP
#include <memfault/metrics/connectivity.h>
#endif

#include "epacket_internal.h"

#define DT_DRV_COMPAT embeint_epacket_udp

#define UDP_PAYLOAD(max_pkt) EPACKET_INTERFACE_PAYLOAD_FROM_PACKET(DT_DRV_INST(0), max_pkt)

enum {
	UDP_STATE_L4_CONNECTED = BIT(0),
	UDP_STATE_VALID_DNS = BIT(1),
	UDP_STATE_SOCKET_OPEN = BIT(2),
	UDP_STATE_CLIENTS_NOTIFIED_UP = BIT(3),
};

struct udp_state {
#ifdef CONFIG_EPACKET_INTERFACE_UDP_DOWNLINK_WATCHDOG
	struct k_work_delayable downlink_watchdog;
	struct net_mgmt_event_callback iface_admin_cb;
#endif
	struct net_mgmt_event_callback l4_callback;
	struct sockaddr remote;
	struct k_event state;
#ifdef CONFIG_EPACKET_INTERFACE_UDP_DETECT_UNACKNOWLEDGED
	struct {
		sys_slist_t tx_waiting;
		struct k_spinlock list_lock;
	} ack_handling;
#endif /* CONFIG_EPACKET_INTERFACE_UDP_DETECT_UNACKNOWLEDGED */
	uint32_t last_receive;
	uint16_t ack_countdown;
	socklen_t remote_len;
	uint16_t remote_port;
	uint16_t iface_max_pkt;
	uint16_t iface_flags;
	int sock;
};
static struct udp_state udp_state;

LOG_MODULE_REGISTER(epacket_udp, CONFIG_EPACKET_UDP_LOG_LEVEL);

#ifdef CONFIG_EPACKET_INTERFACE_UDP_DOWNLINK_WATCHDOG

void epacket_udp_flags_set(uint16_t flags)
{
	udp_state.iface_flags = flags;
}

static void udp_downlink_watchdog_expiry(struct k_work *work)
{
	LOG_WRN("Downlink watchdog expired");
	/* Watchdog expired, reboot */
	infuse_reboot(INFUSE_REBOOT_SW_WATCHDOG, (uintptr_t)udp_downlink_watchdog_expiry,
		      CONFIG_EPACKET_INTERFACE_UDP_DOWNLINK_WATCHDOG_TIMEOUT);
}

static void if_admin_event_handler(struct net_mgmt_event_callback *cb, uint32_t event,
				   struct net_if *iface)
{
	struct udp_state *state = CONTAINER_OF(cb, struct udp_state, iface_admin_cb);

	/* Ignore interfaces that the connection manager is ignoring */
	if (conn_mgr_is_iface_ignored(iface)) {
		LOG_DBG("Ignoring %08x on ignored interface", event);
		return;
	}

	/* Interface is not intended to be persistent, so don't restart the
	 * watchdog on every interface cycle. Instead, start it on the first
	 * event and keep it running as a global watchdog.
	 */
	if (conn_mgr_if_is_bound(iface) && !conn_mgr_if_get_flag(iface, CONN_MGR_IF_PERSISTENT) &&
	    k_work_delayable_busy_get(&state->downlink_watchdog)) {
		LOG_DBG("Ignoring %08x on non-persistent interface", event);
		return;
	}

	if (event == NET_EVENT_IF_ADMIN_UP) {
		/* Application wants the interface connected, start the watchdog */
		LOG_INF("Downlink watchdog started (%d sec)",
			CONFIG_EPACKET_INTERFACE_UDP_DOWNLINK_WATCHDOG_TIMEOUT);
		k_work_schedule(&state->downlink_watchdog,
				K_SECONDS(CONFIG_EPACKET_INTERFACE_UDP_DOWNLINK_WATCHDOG_TIMEOUT));
	} else if (event == NET_EVENT_IF_ADMIN_DOWN) {
		/* Application no longer wants the interface connected, cancel the watchdog */
		LOG_INF("Downlink watchdog cancelled");
		k_work_cancel_delayable(&state->downlink_watchdog);
	}
}

#endif /* CONFIG_EPACKET_INTERFACE_UDP_DOWNLINK_WATCHDOG */

static void cleanup_interface(const struct device *epacket_udp)
{
	struct epacket_interface_common_data *data = epacket_udp->data;
	struct epacket_interface_cb *cb;

	if (k_event_clear(&udp_state.state, UDP_STATE_SOCKET_OPEN)) {
		(void)zsock_close(udp_state.sock);
		LOG_DBG("Closed %d", udp_state.sock);
	}
	if (k_event_clear(&udp_state.state, UDP_STATE_CLIENTS_NOTIFIED_UP)) {
		/* Interface is now disconnected */
		SYS_SLIST_FOR_EACH_CONTAINER(&data->callback_list, cb, node) {
			if (cb->interface_state) {
				cb->interface_state(0, cb->user_ctx);
			}
		}
	}
}

static void l4_event_handler(struct net_mgmt_event_callback *cb, uint32_t event,
			     struct net_if *iface)
{
	uint16_t iface_max_pkt;
	uint16_t iface_mtu;

	if (event == NET_EVENT_L4_CONNECTED) {
		iface_mtu = net_if_get_mtu(iface);
		iface_max_pkt = iface_mtu - NET_IPV4UDPH_LEN;
		udp_state.iface_max_pkt = MIN(iface_max_pkt, CONFIG_EPACKET_PACKET_SIZE_MAX);
		LOG_INF("Network connected (MTU %d, PKT %d)", iface_mtu, udp_state.iface_max_pkt);
		k_event_post(&udp_state.state, UDP_STATE_L4_CONNECTED);
	} else if (event == NET_EVENT_L4_DISCONNECTED) {
		k_event_clear(&udp_state.state, UDP_STATE_L4_CONNECTED);
		cleanup_interface(DEVICE_DT_GET(DT_DRV_INST(0)));
		LOG_INF("Network disconnected");
	}
}

static int epacket_udp_dns_query(void)
{
	KV_STRING_CONST(udp_url_default, CONFIG_EPACKET_INTERFACE_UDP_DEFAULT_URL);
	KV_KEY_TYPE(KV_KEY_EPACKET_UDP_PORT)
	udp_port, udp_port_default = {CONFIG_EPACKET_INTERFACE_UDP_DEFAULT_PORT};
	KV_KEY_TYPE_VAR(KV_KEY_EPACKET_UDP_URL, 64) udp_url;

	/* Load configuration from KV store */
	KV_STORE_READ_FALLBACK(KV_KEY_EPACKET_UDP_PORT, &udp_port, &udp_port_default);
	KV_STORE_READ_FALLBACK(KV_KEY_EPACKET_UDP_URL, &udp_url, &udp_url_default);

	/* Get IP address from DNS */
	return infuse_sync_dns(udp_url.server.value, udp_port.port, AF_INET, SOCK_DGRAM,
			       &udp_state.remote, &udp_state.remote_len);
}

static int epacket_udp_loop(void *a, void *b, void *c)
{
	const struct device *epacket_udp = DEVICE_DT_GET(DT_DRV_INST(0));
	struct epacket_interface_common_data *data = epacket_udp->data;
	struct sockaddr_in local_addr = {0};
	struct epacket_rx_metadata *meta;
	struct epacket_interface_cb *cb;
	struct zsock_pollfd pollfds[1];
	bool first_connection = true;
	struct sockaddr from;
	socklen_t from_len;
	struct net_buf *buf;
	uint16_t port;
	uint8_t *addr;
	int received;
	int rc;

	local_addr.sin_family = AF_INET;
	local_addr.sin_port = htons(6200);

	for (;;) {
		/* Wait until we have network connectivity */
		k_event_wait(&udp_state.state, UDP_STATE_L4_CONNECTED, false, K_FOREVER);

		/* Get IP address of UDP server */
		if (!k_event_test(&udp_state.state, UDP_STATE_VALID_DNS)) {
			MEMFAULT_METRIC_ADD(epacket_udp_dns_query, 1);
			rc = epacket_udp_dns_query();
			if (rc < 0) {
				MEMFAULT_METRIC_ADD(epacket_udp_dns_failure, 1);
				LOG_ERR("DNS lookup failed (%d)", rc);
				goto socket_error;
			}
			k_event_post(&udp_state.state, UDP_STATE_VALID_DNS);
		}

		/* Create the UDP socket */
		udp_state.sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (udp_state.sock < 0) {
			MEMFAULT_METRIC_ADD(epacket_udp_sock_setup_error, 1);
			LOG_ERR("Failed to open socket (%d)", errno);
			goto socket_error;
		}
		k_event_post(&udp_state.state, UDP_STATE_SOCKET_OPEN);
		LOG_DBG("Opened %d", udp_state.sock);

		/* Init ACK countdown */
		udp_state.ack_countdown = CONFIG_EPACKET_INTERFACE_UDP_ACK_COUNTDOWN;

		/* Bind so we can receive downlink packets */
		rc = zsock_bind(udp_state.sock, (struct sockaddr *)&local_addr, sizeof(local_addr));
		if (rc < 0) {
			MEMFAULT_METRIC_ADD(epacket_udp_sock_setup_error, 1);
			LOG_ERR("Failed to bind socket (%d)", errno);
			goto socket_error;
		}
		LOG_INF("Waiting for UDP packets on port %d", ntohs(local_addr.sin_port));
		MEMFAULT_METRIC_TIMER_START(epacket_udp_connected);

		/* Interface is now connected */
		SYS_SLIST_FOR_EACH_CONTAINER(&data->callback_list, cb, node) {
			if (cb->interface_state) {
				cb->interface_state(UDP_PAYLOAD(udp_state.iface_max_pkt),
						    cb->user_ctx);
			}
		}
		k_event_post(&udp_state.state, UDP_STATE_CLIENTS_NOTIFIED_UP);

		pollfds[0].fd = udp_state.sock;
		pollfds[0].events = ZSOCK_POLLIN;

		if (first_connection) {
			/* On the first connection after boot, remind the cloud of key state */
			(void)epacket_send_key_ids(epacket_udp, K_NO_WAIT);
			first_connection = false;
		}

		while (true) {
			/* Wait for data to arrive */
			rc = zsock_poll(pollfds, 1, SYS_FOREVER_MS);
			if (pollfds[0].revents & (ZSOCK_POLLHUP | ZSOCK_POLLNVAL)) {
				LOG_WRN("Socket closed (0x%02X)", pollfds[0].revents);
				break;
			}

			/* Allocate buffer and receive data */
			buf = epacket_alloc_rx(K_SECONDS(30));
			if (buf == NULL) {
#ifdef CONFIG_INFUSE_REBOOT
				/* Could not claim a RX buffer even with an excessive timeout */
				infuse_reboot_delayed(INFUSE_REBOOT_SW_WATCHDOG,
						      (uintptr_t)epacket_udp_loop, 30,
						      K_SECONDS(2));
				k_sleep(K_FOREVER);
#else
				LOG_ERR("UDP thread blocked on RX buffer");
				buf = epacket_alloc_rx(K_FOREVER);
#endif
			}
			from_len = sizeof(from);
			received = zsock_recvfrom(udp_state.sock, buf->data, buf->size, 0, &from,
						  &from_len);
			if (received < 0) {
				LOG_ERR("Failed to receive (%d)", errno);
				net_buf_unref(buf);
				break;
			}
			net_buf_add(buf, received);
			addr = ((struct sockaddr_in *)&from)->sin_addr.s4_addr;
			port = ((struct sockaddr_in *)&from)->sin_port;
			LOG_DBG("Received %d bytes from %d.%d.%d.%d:%d", received, addr[0], addr[1],
				addr[2], addr[3], ntohs(port));

#ifdef CONFIG_MEMFAULT_INFUSE_METRICS_SYNC_SUCCESS_EPACKET_UDP
			memfault_metrics_connectivity_record_sync_success();
#endif

			meta = net_buf_user_data(buf);
			meta->interface = epacket_udp;
			meta->interface_id = EPACKET_INTERFACE_UDP;
			meta->rssi = 0;

			/* Hand off to core ePacket functions */
			epacket_raw_receive_handler(buf);
		}
		MEMFAULT_METRIC_TIMER_STOP(epacket_udp_connected);
socket_error:
		/* Close socket if still open */
		cleanup_interface(epacket_udp);
		k_sleep(K_SECONDS(1));
	}
	return 0;
}

K_THREAD_DEFINE(epacket_udp_thread, 2048, epacket_udp_loop, NULL, NULL, NULL, 0, K_ESSENTIAL, 0);

#ifdef CONFIG_EPACKET_INTERFACE_UDP_DETECT_UNACKNOWLEDGED
static void tx_ack_timeout(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct epacket_tx_metadata *tx_meta =
		CONTAINER_OF(dwork, struct epacket_tx_metadata, dwork);
	struct net_buf *waiting, *found = NULL;

	K_SPINLOCK(&udp_state.ack_handling.list_lock) {
		SYS_SLIST_FOR_EACH_CONTAINER(&udp_state.ack_handling.tx_waiting, waiting, node) {
			if (tx_meta == net_buf_user_data(waiting)) {
				found = waiting;
				break;
			}
		}
		if (found) {
			/* Remove from the list under the lock */
			sys_slist_find_and_remove(&udp_state.ack_handling.tx_waiting, &found->node);
		}
	}
	if (!found) {
		return;
	}

	LOG_DBG("ACK timeout for %d", tx_meta->sequence);
#ifdef CONFIG_EPACKET_INTERFACE_UDP_DECRYPT_TX_FAILURES
	/* Decrypt the failing packet so the handler can decode the payload */
	(void)epacket_udp_tx_decrypt(found);
#endif /* CONFIG_EPACKET_INTERFACE_UDP_DECRYPT_TX_FAILURES */
	epacket_notify_tx_result(DEVICE_DT_GET(DT_DRV_INST(0)), found, -ENODATA);
	net_buf_unref(found);
}

static void tx_pending_ack_handle(const struct device *dev, struct net_buf *buf)
{
	struct epacket_tx_metadata *tx_meta;
	struct net_buf *waiting, *found = NULL;
	uint16_t rx_sequence;

	/* Get the incoming sequence number */
	if (buf->len != sizeof(uint16_t)) {
		LOG_WRN("ACK with unexpected length (%d)", buf->len);
		return;
	}
	rx_sequence = sys_get_le16(buf->data);

	K_SPINLOCK(&udp_state.ack_handling.list_lock) {
		/* Scan our pending packets to see if any match */
		SYS_SLIST_FOR_EACH_CONTAINER(&udp_state.ack_handling.tx_waiting, waiting, node) {
			tx_meta = net_buf_user_data(waiting);
			if (tx_meta->sequence == rx_sequence) {
				found = waiting;
				break;
			}
		}
		if (found) {
			/* Remove from the list under the lock */
			sys_slist_find_and_remove(&udp_state.ack_handling.tx_waiting, &found->node);
		}
	}
	if (!found) {
		return;
	}

	LOG_DBG("ACK received for %d", tx_meta->sequence);
	k_work_cancel_delayable(&tx_meta->dwork);
	epacket_notify_tx_result(dev, found, 0);
	net_buf_unref(found);
}

#else
static void tx_pending_ack_handle(const struct device *dev, struct net_buf *buf)
{
}
#endif /* CONFIG_EPACKET_INTERFACE_UDP_DETECT_UNACKNOWLEDGED */

static void epacket_udp_send(const struct device *dev, struct net_buf *buf)
{
	struct epacket_tx_metadata *meta = net_buf_user_data(buf);
	__maybe_unused bool user_ack_request = meta->flags & EPACKET_FLAGS_ACK_REQUEST;
	ssize_t rc;

	/* Don't do work unless the socket is open */
	if (!k_event_test(&udp_state.state, UDP_STATE_SOCKET_OPEN)) {
		LOG_DBG("No socket");
		rc = -ENOTCONN;
		goto end;
	}

	/* Handle ACK request */
	if ((k_uptime_seconds() - udp_state.last_receive) >=
	    CONFIG_EPACKET_INTERFACE_UDP_ACK_PERIOD_SEC) {

		/* Never receive an ACK after requesting */
		if (udp_state.ack_countdown == 0) {
			LOG_INF("Disconnecting due to no RX packets");
			cleanup_interface(dev);
			/* Force a requery of DNS */
			(void)k_event_clear(&udp_state.state, UDP_STATE_VALID_DNS);
#ifdef CONFIG_MEMFAULT_INFUSE_METRICS_SYNC_SUCCESS_EPACKET_UDP
			/* Notify Memfault backend of sync failure */
			memfault_metrics_connectivity_record_sync_failure();
#endif
			rc = -ENOTCONN;
			goto end;
		}
		/* Add ACK_REQUEST flag to packet */
		LOG_DBG("Requesting ACK on packet");
		meta->flags |= EPACKET_FLAGS_ACK_REQUEST;
		udp_state.ack_countdown--;
	}

	/* Add any set flags */
	meta->flags |= udp_state.iface_flags;

	/* Encrypt the payload */
	if (epacket_udp_encrypt(buf) < 0) {
		LOG_WRN("Failed to encrypt");
		rc = -EIO;
		goto end;
	}

	/* Send to remote server */
	LOG_DBG("Sending %d bytes to server (Type: %d)", buf->len, meta->type);
	rc = zsock_sendto(udp_state.sock, buf->data, buf->len, 0, &udp_state.remote,
			  udp_state.remote_len);
	if (rc == -1) {
		LOG_WRN("Failed to send (%d)", errno);
#ifdef CONFIG_EPACKET_INTERFACE_UDP_DECRYPT_TX_FAILURES
		/* Decrypt the failing packet so the handler can decode the payload */
		(void)epacket_udp_tx_decrypt(buf);
#endif /* CONFIG_EPACKET_INTERFACE_UDP_DECRYPT_TX_FAILURES */
		rc = -errno;
	} else {
		rc = 0;
	}
end:
#ifdef CONFIG_EPACKET_INTERFACE_UDP_DETECT_UNACKNOWLEDGED
	if (rc != 0 || !user_ack_request) {
		epacket_notify_tx_result(dev, buf, rc);
		net_buf_unref(buf);
	} else {
		k_timeout_t timeout =
			K_MSEC(CONFIG_EPACKET_INTERFACE_UDP_DETECT_UNACKNOWLEDGED_TIMEOUT_MS);

		LOG_DBG("Waiting for ACK on %d", meta->sequence);
		k_work_init_delayable(&meta->dwork, tx_ack_timeout);
		k_work_schedule(&meta->dwork, timeout);
		/* User requested an ACK and the packet was sent */
		K_SPINLOCK(&udp_state.ack_handling.list_lock) {
			sys_slist_append(&udp_state.ack_handling.tx_waiting, &buf->node);
		}
	}
#else
	epacket_notify_tx_result(dev, buf, rc);
	net_buf_unref(buf);
#endif /* CONFIG_EPACKET_INTERFACE_UDP_DETECT_UNACKNOWLEDGED */
}

static void epacket_udp_decrypt_res(const struct device *dev, struct net_buf *buf, int decrypt_res)
{
	struct epacket_rx_metadata *meta = net_buf_user_data(buf);

	if (decrypt_res == 0) {
		/* Update ACK state */
		udp_state.last_receive = k_uptime_seconds();
		udp_state.ack_countdown = CONFIG_EPACKET_INTERFACE_UDP_ACK_COUNTDOWN;
#ifdef CONFIG_EPACKET_INTERFACE_UDP_DOWNLINK_WATCHDOG
		/* Feed the downlink watchdog */
		k_work_reschedule(
			&udp_state.downlink_watchdog,
			K_SECONDS(CONFIG_EPACKET_INTERFACE_UDP_DOWNLINK_WATCHDOG_TIMEOUT));
#endif /* CONFIG_EPACKET_INTERFACE_UDP_DOWNLINK_WATCHDOG */

		/* Handle pending ACKs */
		if (meta->type == INFUSE_ACK) {
			tx_pending_ack_handle(dev, buf);
		}
	} else {
		/* Decryption failed, try to send a KEY_IDS packet to notify the cloud side
		 * that device/network keys may have changed.
		 */
		(void)epacket_send_key_ids(dev, K_NO_WAIT);
	}
}

static uint16_t epacket_udp_max_packet(const struct device *dev)
{
	return k_event_test(&udp_state.state, UDP_STATE_SOCKET_OPEN) ? udp_state.iface_max_pkt : 0;
}

#ifdef CONFIG_ZTEST

void epacket_udp_dns_reset(void)
{
	k_event_clear(&udp_state.state, UDP_STATE_VALID_DNS);
	udp_state.last_receive = k_uptime_seconds();
	udp_state.ack_countdown = CONFIG_EPACKET_INTERFACE_UDP_ACK_COUNTDOWN;
}

#endif /* CONFIG_ZTEST */

static int epacket_udp_init(const struct device *dev)
{
	epacket_interface_common_init(dev);
	k_event_init(&udp_state.state);
#ifdef CONFIG_EPACKET_INTERFACE_UDP_DETECT_UNACKNOWLEDGED
	sys_slist_init(&udp_state.ack_handling.tx_waiting);
#endif /* CONFIG_EPACKET_INTERFACE_UDP_DETECT_UNACKNOWLEDGED */
#ifdef CONFIG_EPACKET_INTERFACE_UDP_DOWNLINK_WATCHDOG
	k_work_init_delayable(&udp_state.downlink_watchdog, udp_downlink_watchdog_expiry);

	/* Register for callbacks on interface admin (application requested) state change */
	net_mgmt_init_event_callback(&udp_state.iface_admin_cb, if_admin_event_handler,
				     NET_EVENT_IF_ADMIN_UP | NET_EVENT_IF_ADMIN_DOWN);
	net_mgmt_add_event_callback(&udp_state.iface_admin_cb);
#endif /* CONFIG_EPACKET_INTERFACE_UDP_DOWNLINK_WATCHDOG */

	/* Register for callbacks on network connectivity */
	net_mgmt_init_event_callback(&udp_state.l4_callback, l4_event_handler,
				     NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED);
	net_mgmt_add_event_callback(&udp_state.l4_callback);

	return 0;
}

static const struct epacket_interface_api udp_api = {
	.send = epacket_udp_send,
	.decrypt_result = epacket_udp_decrypt_res,
	.max_packet_size = epacket_udp_max_packet,
};

BUILD_ASSERT(sizeof(struct epacket_udp_frame) == DT_INST_PROP(0, header_size));
static struct epacket_interface_common_data epacket_udp_data;
static const struct epacket_interface_common_config epacket_udp_config = {
	.max_packet_size = CONFIG_EPACKET_PACKET_SIZE_MAX,
	.header_size = DT_INST_PROP(0, header_size),
	.footer_size = DT_INST_PROP(0, footer_size),
};
DEVICE_DT_DEFINE(DT_DRV_INST(0), epacket_udp_init, NULL, &epacket_udp_data, &epacket_udp_config,
		 POST_KERNEL, 0, &udp_api);
