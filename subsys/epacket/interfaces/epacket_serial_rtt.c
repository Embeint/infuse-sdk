/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_serial.h>

#include <SEGGER_RTT.h>

#include "epacket_internal.h"

#define DT_DRV_COMPAT embeint_epacket_serial

#define POLL_PERIOD_MS 500

struct epacket_serial_config {
	struct epacket_interface_common_config common;
	const struct device *backend;
};

struct epacket_serial_data {
	struct epacket_interface_common_data common_data;
	const struct device *interface;
	struct k_work_delayable poll_work;
};

LOG_MODULE_REGISTER(epacket_serial, CONFIG_EPACKET_SERIAL_LOG_LEVEL);

static void poll_worker(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct epacket_serial_data *data =
		CONTAINER_OF(dwork, struct epacket_serial_data, poll_work);
	uint8_t buffer[64];
	int recv_len;

	do {
		/* Read the endpoint buffer size */
		recv_len = SEGGER_RTT_Read(0, buffer, sizeof(buffer));
		/* Extract ePacket packets */
		if (recv_len > 0) {
			epacket_serial_reconstruct(data->interface, buffer, recv_len,
						   epacket_raw_receive_handler);
		}
	} while (recv_len);

	/* Reshedule poll */
	k_work_reschedule(dwork, K_MSEC(POLL_PERIOD_MS));
}

static void epacket_serial_send(const struct device *dev, struct net_buf *buf)
{
	struct epacket_serial_frame_header *header;

	/* Encrypt the payload */
	if (epacket_serial_encrypt(buf) < 0) {
		LOG_WRN("Failed to encrypt");
		epacket_notify_tx_result(dev, buf, -EIO);
		net_buf_unref(buf);
		return;
	}

	/* Push frame header on */
	header = net_buf_push(buf, sizeof(*header));
	*header = (struct epacket_serial_frame_header){
		.sync = {EPACKET_SERIAL_SYNC_A, EPACKET_SERIAL_SYNC_B},
		.len = buf->len - sizeof(*header),
	};

	/* Push packet at RTT */
	SEGGER_RTT_Write(0, buf->data, buf->len);
	net_buf_unref(buf);

	/* Small delay to give debugger a chance to read out the packet.
	 * Removing this will result in silently dropped packets when
	 * bursts of packets are sent due to the behaviour of SEGGER_RTT_Write.
	 */
	k_sleep(K_MSEC(5));
	epacket_notify_tx_result(dev, buf, 0);
}

static int epacket_receive_control(const struct device *dev, bool enable)
{
	struct epacket_serial_data *data = dev->data;

	if (enable) {
		k_work_schedule(&data->poll_work, K_NO_WAIT);
	} else {
		k_work_cancel_delayable(&data->poll_work);
	}
	return 0;
}

static int epacket_serial_rtt_init(const struct device *dev)
{
	struct epacket_serial_data *data = dev->data;

	data->interface = dev;
	epacket_interface_common_init(dev);
	k_work_init_delayable(&data->poll_work, poll_worker);
	return 0;
}

static const struct epacket_interface_api serial_api = {
	.send = epacket_serial_send,
	.receive_ctrl = epacket_receive_control,
};

#define EPACKET_SERIAL_RTT_DEFINE(inst)                                                            \
	BUILD_ASSERT(sizeof(struct epacket_serial_frame_header) +                                  \
			     sizeof(struct epacket_serial_frame) ==                                \
		     DT_INST_PROP(inst, header_size));                                             \
	static struct epacket_serial_data serial_data_##inst;                                      \
	static const struct epacket_serial_config serial_config_##inst = {                         \
		.common =                                                                          \
			{                                                                          \
				.max_packet_size =                                                 \
					EPACKET_INTERFACE_MAX_PACKET(DT_DRV_INST(inst)),           \
				.header_size = DT_INST_PROP(inst, header_size),                    \
				.footer_size = DT_INST_PROP(inst, footer_size),                    \
			},                                                                         \
		.backend = DEVICE_DT_GET(DT_INST_PROP(inst, serial)),                              \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, epacket_serial_rtt_init, NULL, &serial_data_##inst,            \
			      &serial_config_##inst, POST_KERNEL, 0, &serial_api);

DT_INST_FOREACH_STATUS_OKAY(EPACKET_SERIAL_RTT_DEFINE)
