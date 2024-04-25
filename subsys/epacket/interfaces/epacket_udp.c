/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/dns_resolve.h>
#include <zephyr/random/random.h>

#include <infuse/identifiers.h>
#include <infuse/time/civil.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_udp.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

#include "epacket_internal.h"

#define DT_DRV_COMPAT embeint_epacket_udp

enum {
	UDP_STATE_L4_CONNECTED = BIT(0),
	UDP_STATE_VALID_DNS = BIT(1),
	UDP_STATE_SOCKET_OPEN = BIT(2),
};

struct udp_state {
	struct net_mgmt_event_callback l4_callback;
	struct sockaddr remote;
	struct k_event state;
	socklen_t remote_len;
	uint16_t remote_port;
	int sock;
};
static struct udp_state udp_state;

LOG_MODULE_REGISTER(epacket_udp, CONFIG_EPACKET_UDP_LOG_LEVEL);

static void l4_event_handler(struct net_mgmt_event_callback *cb, uint32_t event, struct net_if *iface)
{
	if (event == NET_EVENT_L4_CONNECTED) {
		LOG_INF("Network connected");
		k_event_post(&udp_state.state, UDP_STATE_L4_CONNECTED);
	} else if (event == NET_EVENT_L4_DISCONNECTED) {
		k_event_clear(&udp_state.state, UDP_STATE_L4_CONNECTED);
		LOG_INF("Network disconnected");
		if (k_event_clear(&udp_state.state, UDP_STATE_SOCKET_OPEN)) {
			(void)close(udp_state.sock);
			LOG_DBG("Closed %d", udp_state.sock);
		}
	}
}

static int epacket_udp_dns_query(void)
{
	KV_STRING_CONST(udp_url_default, CONFIG_EPACKET_INTERFACE_UDP_DEFAULT_URL);
	KV_KEY_TYPE(KV_KEY_EPACKET_UDP_PORT) udp_port, udp_port_default = {CONFIG_EPACKET_INTERFACE_UDP_DEFAULT_PORT};
	KV_KEY_TYPE_VAR(KV_KEY_EPACKET_UDP_URL, 64) udp_url;
	struct zsock_addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_DGRAM,
	};
	struct zsock_addrinfo *res = NULL;
	struct sockaddr_in *sockaddr;
	uint8_t *addr;
	int rc;

	/* Load configuration from KV store */
	KV_STORE_READ_FALLBACK(KV_KEY_EPACKET_UDP_PORT, &udp_port, &udp_port_default);
	KV_STORE_READ_FALLBACK(KV_KEY_EPACKET_UDP_URL, &udp_url, &udp_url_default);

	/* Get IP address from DNS */
	rc = zsock_getaddrinfo(udp_url.server.value, NULL, &hints, &res);
	if (rc < 0) {
		return rc;
	}
	udp_state.remote = *res->ai_addr;
	udp_state.remote_len = res->ai_addrlen;
	zsock_freeaddrinfo(res);

	sockaddr = (struct sockaddr_in *)&udp_state.remote;
	sockaddr->sin_family = AF_INET;
	sockaddr->sin_port = htons(udp_port.port);
	addr = sockaddr->sin_addr.s4_addr;

	LOG_INF("%s -> %d.%d.%d.%d:%d", udp_url.server.value, addr[0], addr[1], addr[2], addr[3], udp_port.port);
	return rc;
}

static int epacket_udp_loop(void *a, void *b, void *c)
{
	struct sockaddr_in local_addr = {0};
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
			rc = epacket_udp_dns_query();
			if (rc < 0) {
				LOG_ERR("DNS lookup failed (%d)", rc);
				goto socket_error;
			}
			k_event_post(&udp_state.state, UDP_STATE_VALID_DNS);
		}

		/* Create the UDP socket */
		udp_state.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (udp_state.sock == -1) {
			LOG_ERR("Failed to open socket (%d)", errno);
			goto socket_error;
		}
		k_event_post(&udp_state.state, UDP_STATE_SOCKET_OPEN);
		LOG_DBG("Opened %d", udp_state.sock);

		rc = zsock_bind(udp_state.sock, (struct sockaddr *)&local_addr, sizeof(local_addr));
		if (rc < 0) {
			LOG_ERR("Failed to bind socket (%d)", errno);
			goto socket_error;
		}

		LOG_INF("Waiting for UDP packets on port %d", ntohs(local_addr.sin_port));
		while (true) {
			buf = epacket_alloc_rx(K_FOREVER);
			/* Receive data into local buffer */
			received = zsock_recvfrom(udp_state.sock, buf->data, buf->size, 0, &from, &from_len);
			if (received < 0) {
				LOG_ERR("Failed to receive (%d)", errno);
				net_buf_unref(buf);
				goto socket_error;
			}
			addr = ((struct sockaddr_in *)&from)->sin_addr.s4_addr;
			port = ((struct sockaddr_in *)&from)->sin_port;
			LOG_DBG("Received %d bytes from %d.%d.%d.%d:%d", received, addr[0], addr[1], addr[2], addr[3],
				port);
			struct epacket_receive_metadata meta = {
				.interface = DEVICE_DT_GET(DT_DRV_INST(0)),
				.interface_id = EPACKET_INTERFACE_UDP,
				.rssi = 0,
			};

			/* Hand off to core ePacket functions */
			epacket_raw_receive_handler(&meta, buf);
		}

		/* clang-format off */
socket_error:
		/* clang-format on */

		/* Close socket if still open */
		if (k_event_clear(&udp_state.state, UDP_STATE_SOCKET_OPEN)) {
			(void)zsock_close(udp_state.sock);
		}
		k_sleep(K_SECONDS(1));
	}
	return 0;
}

K_THREAD_DEFINE(epacket_udp_thread, 2048, epacket_udp_loop, NULL, NULL, NULL, 0, K_ESSENTIAL, 0);

static void epacket_udp_packet_overhead(const struct device *dev, size_t *header, size_t *footer)
{
	/* ChaCha20-Poly1305 adds a 16 byte tag after the ciphertext */
	*header = sizeof(struct epacket_udp_frame);
	*footer = 16;
}

static int epacket_udp_send(const struct device *dev, struct net_buf *buf)
{
	ssize_t rc;

	/* Don't do work unless the socket is open */
	if (!k_event_test(&udp_state.state, UDP_STATE_SOCKET_OPEN)) {
		LOG_DBG("No socket");
		rc = -ENOTCONN;
		goto end;
	}

	/* Encrypt the payload */
	if (epacket_udp_encrypt(buf) < 0) {
		LOG_WRN("Failed to encrypt");
		rc = -EINVAL;
		goto end;
	}

	/* Send to remote server */
	rc = zsock_sendto(udp_state.sock, buf->data, buf->len, 0, &udp_state.remote, udp_state.remote_len);
	if (rc == -1) {
		LOG_WRN("Failed to send (%d)", errno);
		rc = -errno;
	} else {
		rc = 0;
	}
end:
	net_buf_unref(buf);
	return rc;
}

static int epacket_udp_init(const struct device *dev)
{
	k_event_init(&udp_state.state);

	/* Register for callbacks on network connectivity */
	net_mgmt_init_event_callback(&udp_state.l4_callback, l4_event_handler,
				     NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED);
	net_mgmt_add_event_callback(&udp_state.l4_callback);

	return 0;
}

static const struct epacket_interface_api udp_api = {
	.packet_overhead = epacket_udp_packet_overhead,
	.send = epacket_udp_send,
};

DEVICE_DT_DEFINE(DT_DRV_INST(0), epacket_udp_init, NULL, NULL, NULL, POST_KERNEL, 0, &udp_api);
