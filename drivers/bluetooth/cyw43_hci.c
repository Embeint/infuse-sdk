/* Copyright (c) 2026 Embeint Holdings Pty Ltd
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#define DT_DRV_COMPAT embeint_cyw43_bt_hci

#include <zephyr/drivers/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <whd_wifi_api.h>
#include <whd_types_int.h>
#include <whd_int.h>
/* The pinned AIROC driver exports its initialized station interface. */
extern whd_interface_t airoc_wifi_get_whd_interface(void);

#include "cyw43_transport.h"

#define CYW43_RESOURCE_ATTRIBUTE
#include <cyw43_btfw_43439.h>

LOG_MODULE_REGISTER(cyw43_hci, CONFIG_BT_HCI_DRIVER_LOG_LEVEL);

struct cyw43_hci_data {
	struct cyw43_transport transport;
	bt_hci_recv_t recv;
	struct k_thread thread;
	atomic_t running;
	bool loaded;
	uint8_t rx[CYW43_PACKET_MAX];
};

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1, "Only one CYW43 HCI is supported");

BUILD_ASSERT(CONFIG_BT_HCI_CYW43_INIT_PRIORITY > CONFIG_WIFI_INIT_PRIORITY);
BUILD_ASSERT(CONFIG_BT_HCI_CYW43_RX_PRIORITY < CONFIG_NUM_PREEMPT_PRIORITIES);

static K_KERNEL_STACK_DEFINE(rx_stack, CONFIG_BT_HCI_CYW43_RX_STACK_SIZE);

static void receive_packet(const struct device *dev, uint8_t type, uint8_t *data, size_t len)
{
	struct cyw43_hci_data *hci = dev->data;
	struct net_buf *buf;

	switch (type) {
	case BT_HCI_H4_EVT:
		if (len < sizeof(struct bt_hci_evt_hdr) || data[1] + 2 != len) {
			LOG_ERR("Invalid HCI event length");
			return;
		}
		buf = bt_buf_get_evt(data[0], false, K_MSEC(100));
		break;
	case BT_HCI_H4_ACL:
		if (len < sizeof(struct bt_hci_acl_hdr) || sys_get_le16(data + 2) + 4 != len) {
			LOG_ERR("Invalid HCI ACL length");
			return;
		}
		buf = bt_buf_get_rx(BT_BUF_ACL_IN, K_MSEC(100));
		break;
	default:
		LOG_WRN("Unsupported HCI packet type %u", type);
		return;
	}
	if (buf == NULL) {
		LOG_ERR("No Bluetooth receive buffer");
		return;
	}
	if (len > net_buf_tailroom(buf)) {
		LOG_ERR("HCI packet exceeds host buffer");
		net_buf_unref(buf);
		return;
	}
	net_buf_add_mem(buf, data, len);
	hci->recv(dev, buf);
}

static void receive_thread(void *arg, void *unused1, void *unused2)
{
	const struct device *dev = arg;
	struct cyw43_hci_data *hci = dev->data;
	uint8_t type;
	int rc;

	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);

	while (atomic_get(&hci->running)) {
		/* Bound each drain so continuous radio traffic cannot monopolize CPU. */
		for (int count = 0; count < 16 && atomic_get(&hci->running); count++) {
			rc = cyw43_transport_recv(&hci->transport, &type, hci->rx, sizeof(hci->rx));
			if (rc <= 0) {
				if (rc < 0 && rc != -EAGAIN) {
					LOG_ERR("Bluetooth receive failed (%d)", rc);
				}
				break;
			}
			receive_packet(dev, type, hci->rx, rc);
		}
		k_sleep(K_MSEC(CONFIG_BT_HCI_CYW43_POLL_INTERVAL_MS));
	}
}

static int cyw43_open(const struct device *dev, bt_hci_recv_t recv)
{
	struct cyw43_hci_data *hci = dev->data;
	const struct device *wifi = DEVICE_DT_GET(DT_INST_PHANDLE(0, wifi));
	whd_interface_t interface;
	int rc;

	if (atomic_get(&hci->running)) {
		return -EALREADY;
	}
	if (!device_is_ready(wifi)) {
		return -ENODEV;
	}
	interface = airoc_wifi_get_whd_interface();
	if (interface == NULL || interface->whd_driver == NULL) {
		return -ENODEV;
	}
	if (!hci->loaded) {
		hci->transport.driver = interface->whd_driver;
		rc = cyw43_transport_open(&hci->transport, cyw43_btfw_43439,
					  sizeof(cyw43_btfw_43439));
		if (rc) {
			LOG_ERR("Bluetooth firmware initialization failed (%d)", rc);
			return rc;
		}
		hci->loaded = true;
	}
	hci->recv = recv;
	atomic_set(&hci->running, 1);
	k_thread_create(&hci->thread, rx_stack, K_KERNEL_STACK_SIZEOF(rx_stack), receive_thread,
			(void *)dev, NULL, NULL, K_PRIO_PREEMPT(CONFIG_BT_HCI_CYW43_RX_PRIORITY), 0,
			K_NO_WAIT);
	k_thread_name_set(&hci->thread, "cyw43_hci_rx");
	return 0;
}

static int cyw43_close(const struct device *dev)
{
	struct cyw43_hci_data *hci = dev->data;

	if (atomic_cas(&hci->running, 1, 0)) {
		k_thread_join(&hci->thread, K_FOREVER);
		hci->recv = NULL;
	}
	return 0;
}

static int cyw43_send(const struct device *dev, struct net_buf *buf)
{
	struct cyw43_hci_data *hci = dev->data;
	int64_t deadline = k_uptime_get() + CONFIG_BT_HCI_CYW43_SEND_TIMEOUT_MS;
	int rc;

	if (!atomic_get(&hci->running)) {
		return -ENETDOWN;
	}
	/* Zephyr supplies H:4 in the first byte. Leave the caller's buffer intact
	 * on failure; ownership transfers to the driver only after success.
	 */
	if (buf->len < 2 || (buf->data[0] != BT_HCI_H4_CMD && buf->data[0] != BT_HCI_H4_ACL)) {
		return -EINVAL;
	}
	do {
		rc = cyw43_transport_send(&hci->transport, buf->data[0], buf->data + 1,
					  buf->len - 1);
		if (rc != -EAGAIN) {
			break;
		}
		k_sleep(K_MSEC(1));
	} while (k_uptime_get() < deadline);
	if (rc == 0) {
		net_buf_unref(buf);
	}
	return rc;
}

static DEVICE_API(bt_hci, cyw43_api) = {
	.open = cyw43_open,
	.close = cyw43_close,
	.send = cyw43_send,
};

static struct cyw43_hci_data hci_data;

DEVICE_DT_INST_DEFINE(0, NULL, NULL, &hci_data, NULL, POST_KERNEL,
		      CONFIG_BT_HCI_CYW43_INIT_PRIORITY, &cyw43_api);
