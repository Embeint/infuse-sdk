/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>
#include <stdint.h>

#include <zephyr/random/random.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>

#include <infuse/time/epoch.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/keys.h>
#include <infuse/epacket/interface/epacket_serial.h>
#include <infuse/security.h>

#include "epacket_internal.h"

#ifdef CONFIG_EPACKET_INTERFACE_ZTEST_PACKETS
LOG_MODULE_REGISTER(epacket_serial, LOG_LEVEL_INF);
#else
LOG_MODULE_DECLARE(epacket_serial, CONFIG_EPACKET_SERIAL_LOG_LEVEL);
#endif

static const uint8_t sync_bytes[2] = {EPACKET_SERIAL_SYNC_A, EPACKET_SERIAL_SYNC_B};

static uint16_t epacket_serial_prefix_search(uint16_t pkt_idx, uint16_t *payload_remaining,
					     uint8_t byte)
{
	static uint8_t len_lsb;

	switch (pkt_idx) {
	case 0:
	case 1:
		/* SYNC bytes */
		if (byte != sync_bytes[pkt_idx]) {
			return 0;
		}
		break;
	case 2:
		/* First byte of payload length field */
		len_lsb = byte;
		break;
	case 3:
		/* Second byte of payload length field */
		*payload_remaining = ((uint16_t)byte << 8) | len_lsb;
		if (*payload_remaining == 0) {
			/* Empty payload is invalid */
			return 0;
		}
		break;
	}
	return pkt_idx + 1;
}

void epacket_serial_reconstruct(const struct device *dev, uint8_t *buffer, size_t len,
				void (*handler)(struct net_buf *))
{
	struct epacket_rx_metadata *meta;
	static struct net_buf *rx_buffer;
	static uint16_t payload_remaining;
	static uint16_t pkt_idx;

	for (int i = 0; i < len; i++) {
		/* Search for packet header */
		pkt_idx = epacket_serial_prefix_search(pkt_idx, &payload_remaining, buffer[i]);
		if (pkt_idx <= 4) {
			continue;
		}

		/* Allocate RX buffer */
		if (pkt_idx == 5) {
			if (payload_remaining > CONFIG_EPACKET_PACKET_SIZE_MAX) {
				LOG_WRN("Payload %d too large", payload_remaining);
			} else {
				/* Can't block in interrupts */
				rx_buffer = epacket_alloc_rx(K_NO_WAIT);
				if (rx_buffer == NULL) {
					LOG_WRN("Dropping packet");
				}
			}
		}
		/* Payload bytes */
		uint16_t to_add = MIN(payload_remaining, len - i);

		/* No more data in provided buffer */
		if (to_add == 0) {
			return;
		}

		/* Add payload to buffer */
		if (rx_buffer) {
			net_buf_add_mem(rx_buffer, buffer + i, to_add);
		}
		pkt_idx += to_add;
		payload_remaining -= to_add;
		i += to_add - 1;

		/* Still more bytes to come after exhausting this input buffer */
		if (payload_remaining) {
			return;
		}

		/* All data received, add metadata and ship it */
		if (rx_buffer) {
			meta = net_buf_user_data(rx_buffer);
			meta->interface = dev;
			meta->interface_id = EPACKET_INTERFACE_SERIAL;
			meta->rssi = 0;

			/* Hand off to core ePacket functions */
			handler(rx_buffer);
		}
		/* Reset parsing state */
		rx_buffer = NULL;
		pkt_idx = 0;
	}
}

int epacket_serial_encrypt(struct net_buf *buf)
{
	return epacket_versioned_v0_encrypt(buf, EPACKET_KEY_INTERFACE_SERIAL);
}

int epacket_serial_decrypt(struct net_buf *buf)
{
	return epacket_versioned_v0_decrypt(buf, EPACKET_KEY_INTERFACE_SERIAL);
}
