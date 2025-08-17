/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/net_buf.h>
#include <zephyr/bluetooth/buf.h>
#include <zephyr/bluetooth/hci_vs.h>

#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface.h>
#include <infuse/bluetooth/infuse_hci_vs.h>

#include "epacket_internal.h"

#define DT_DRV_COMPAT embeint_epacket_hci

struct epacket_hci_frame {
	infuse_hci_cmd_vs_epacket_t header;
} __packed;

LOG_MODULE_REGISTER(epacket_hci, CONFIG_EPACKET_HCI_LOG_LEVEL);

#ifdef CONFIG_BT_HCI_RAW
/* Bluetooth controller side of link */

#ifdef CONFIG_BT_LL_SOFTDEVICE
#include "sdc_hci.h"
#include "../subsys/bluetooth/controller/hci_internal.h"
#else
#error Unknown controller implementation
#endif

uint8_t infuse_hci_cmd_vs_epacket(const infuse_hci_cmd_vs_epacket_t *p_params, uint8_t len)
{
	struct net_buf *buf = epacket_alloc_rx(K_NO_WAIT);
	struct epacket_rx_metadata *meta;

	if (buf == NULL) {
		return BT_HCI_ERR_MEM_CAPACITY_EXCEEDED;
	}

	meta = net_buf_user_data(buf);
	meta->interface = DEVICE_DT_GET(DT_DRV_INST(0));
	meta->interface_id = EPACKET_INTERFACE_HCI;
	meta->rssi = 0;

	LOG_HEXDUMP_DBG(p_params, len, "RX");

	/* Push payload into buffer */
	net_buf_add_mem(buf, p_params, len);

	/* Hand off to core ePacket functions */
	epacket_raw_receive_handler(buf);

	return BT_HCI_ERR_SUCCESS;
}

uint8_t epacket_handler(uint8_t const *cmd, uint8_t *raw_event_out, uint8_t *param_length_out,
			bool *gives_cmd_status)
{
	uint8_t const *cmd_params = &cmd[BT_HCI_CMD_HDR_SIZE];
	uint16_t opcode = sys_get_le16(cmd);

	switch (opcode) {
	case INFUSE_HCI_OPCODE_CMD_VS_EPACKET:
		*gives_cmd_status = true;
		return infuse_hci_cmd_vs_epacket((const void *)cmd_params, cmd[2]);
	default:
		return BT_HCI_ERR_UNKNOWN_CMD;
	}
}

static void infuse_hci_link_init(void)
{
	hci_internal_user_cmd_handler_register(epacket_handler);
}

/* Inject buffer into controller output stream */
int bt_hci_recv(const struct device *dev, struct net_buf *buf);

static void epacket_hci_send(const struct device *dev, struct net_buf *buf)
{
	struct epacket_tx_metadata *meta = net_buf_user_data(buf);
	struct net_buf *evt = bt_buf_get_evt(BT_HCI_EVT_VENDOR, false, K_FOREVER);
	struct bt_hci_evt_hdr *evt_hdr;
	struct bt_hci_evt_vs *evt_vs_hdr;
	infuse_hci_cmd_vs_epacket_t *epacket_hdr;
	int rc;

	static uint16_t sequence_num;

	/* HCI event header */
	evt_hdr = net_buf_add(evt, sizeof(*evt_hdr));
	evt_hdr->evt = BT_HCI_EVT_VENDOR;
	evt_hdr->len = sizeof(*evt_vs_hdr) + sizeof(*epacket_hdr) + buf->len;
	/* Vendor Specific event header */
	evt_vs_hdr = net_buf_add(evt, sizeof(*evt_vs_hdr));
	evt_vs_hdr->subevent = INFUSE_HCI_EVT_VS_EPACKET;
	/* ePacket header */
	epacket_hdr = net_buf_add(evt, sizeof(*epacket_hdr));
	epacket_hdr->type = meta->type;
	epacket_hdr->flags = meta->flags;
	epacket_hdr->sequence = sequence_num++;
	/* ePacket payload */
	net_buf_add_mem(evt, buf->data, buf->len);

	LOG_HEXDUMP_DBG(evt->data, evt->len, "TX");

	rc = bt_hci_recv(NULL, evt);
	if (rc < 0) {
		LOG_WRN("Failed to send (%d)", rc);
	}
	epacket_notify_tx_result(dev, buf, rc);
	net_buf_unref(buf);
}

#endif /* CONFIG_BT_HCI_RAW */

#ifdef CONFIG_BT_HCI_HOST
/* Bluetooth host side of link */

static bool infuse_hci_evt_handler(struct net_buf_simple *evt_buf)
{
	struct bt_hci_evt_vs *evt;

	evt = net_buf_simple_pull_mem(evt_buf, sizeof(*evt));

	if (evt->subevent != INFUSE_HCI_EVT_VS_EPACKET) {
		return false;
	}

	LOG_HEXDUMP_DBG(evt_buf->data, evt_buf->len, "RX");

	struct net_buf *buf = epacket_alloc_rx(K_FOREVER);
	struct epacket_rx_metadata *meta;

	meta = net_buf_user_data(buf);
	meta->interface = DEVICE_DT_GET(DT_DRV_INST(0));
	meta->interface_id = EPACKET_INTERFACE_HCI;
	meta->rssi = 0;

	/* Push payload into buffer */
	net_buf_add_mem(buf, evt_buf->data, evt_buf->len);

	/* Hand off to core ePacket functions */
	epacket_raw_receive_handler(buf);
	return true;
}

static void infuse_hci_link_init(void)
{
	bt_hci_register_vnd_evt_cb(infuse_hci_evt_handler);
}

static void epacket_hci_send(const struct device *dev, struct net_buf *buf)
{
	struct epacket_tx_metadata *meta = net_buf_user_data(buf);
	infuse_hci_cmd_vs_epacket_t *epacket_hdr;
	static uint16_t sequence_num;
	struct net_buf *cmd = bt_hci_cmd_create(INFUSE_HCI_OPCODE_CMD_VS_EPACKET,
						sizeof(*epacket_hdr) + buf->len);
	int rc;

	if (cmd == NULL) {
		/* When run from system workqueue */
		rc = -EINVAL;
		goto end;
	}

	/* ePacket header */
	epacket_hdr = net_buf_add(cmd, sizeof(*epacket_hdr));
	epacket_hdr->type = meta->type;
	epacket_hdr->flags = meta->flags;
	epacket_hdr->sequence = sequence_num++;
	/* ePacket payload */
	net_buf_add_mem(cmd, buf->data, buf->len);

	LOG_HEXDUMP_DBG(cmd->data, cmd->len, "TX");

	rc = bt_hci_cmd_send(INFUSE_HCI_OPCODE_CMD_VS_EPACKET, cmd);
	if (rc < 0) {
		LOG_WRN("Failed to send (%d)", rc);
	}
end:
	epacket_notify_tx_result(dev, buf, rc);
	net_buf_unref(buf);
}

#endif /* CONFIG_BT_HCI_HOST */

int epacket_hci_decrypt(struct net_buf *buf)
{
	if (buf->len <= sizeof(struct epacket_hci_frame)) {
		return -EINVAL;
	}

	struct epacket_hci_frame *header = (void *)buf->data;
	struct epacket_rx_metadata *meta = net_buf_user_data(buf);

	net_buf_pull(buf, sizeof(struct epacket_hci_frame));
	meta->auth = EPACKET_AUTH_DEVICE;
	meta->type = header->header.type;
	meta->flags = header->header.flags;
	meta->sequence = 0;
	return 0;
}

static int epacket_hci_init(const struct device *dev)
{
	epacket_interface_common_init(dev);
	infuse_hci_link_init();
	return 0;
}

static const struct epacket_interface_api hci_api = {
	.send = epacket_hci_send,
};

BUILD_ASSERT(sizeof(struct epacket_hci_frame) == DT_INST_PROP(0, header_size));
static struct epacket_interface_common_data epacket_hci_data;
static const struct epacket_interface_common_config epacket_hci_config = {
	.max_packet_size = EPACKET_INTERFACE_MAX_PACKET(DT_DRV_INST(0)),
	.header_size = DT_INST_PROP(0, header_size),
	.footer_size = DT_INST_PROP(0, footer_size),
};
DEVICE_DT_DEFINE(DT_DRV_INST(0), epacket_hci_init, NULL, &epacket_hci_data, &epacket_hci_config,
		 POST_KERNEL, 76, &hci_api);
