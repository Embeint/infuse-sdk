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

#ifdef CONFIG_EPACKET_INTERFACE_ZTEST_PACKETS
/* Tests reconstruct packets without an interface device */
static struct epacket_serial_reconstruct_state ztest_state;
#endif /* CONFIG_EPACKET_INTERFACE_ZTEST_PACKETS */

static struct epacket_serial_reconstruct_state *reconstruct_state(const struct device *dev)
{
#ifdef CONFIG_EPACKET_INTERFACE_ZTEST_PACKETS
	if (dev == NULL) {
		return &ztest_state;
	}
#endif /* CONFIG_EPACKET_INTERFACE_ZTEST_PACKETS */
	struct epacket_serial_common_data *data = dev->data;

	return &data->reconstruct;
}

static uint16_t epacket_serial_prefix_search(struct epacket_serial_reconstruct_state *state,
					     uint8_t byte)
{
	uint16_t pkt_idx = state->pkt_idx;

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
		state->len_lsb = byte;
		break;
	case 3:
		/* Second byte of payload length field */
		state->payload_remaining = ((uint16_t)byte << 8) | state->len_lsb;
		if (state->payload_remaining == 0) {
			/* Empty payload is invalid */
			return 0;
		}
		break;
	}
	return pkt_idx + 1;
}

void epacket_serial_reconstruct_reset(const struct device *dev)
{
	reconstruct_state(dev)->reset = true;
}

void epacket_serial_reconstruct(const struct device *dev, uint8_t *buffer, size_t len,
				void (*handler)(struct net_buf *))
{
	struct epacket_serial_reconstruct_state *state = reconstruct_state(dev);
	struct epacket_rx_metadata *meta;

	/* Reset reconstructor if requested */
	if (state->reset) {
		if (state->rx_buffer) {
			net_buf_unref(state->rx_buffer);
		}
		state->rx_buffer = NULL;
		state->payload_remaining = 0;
		state->pkt_idx = 0;
		state->reset = false;
	}

	for (int i = 0; i < len; i++) {
		/* Search for packet header */
		state->pkt_idx = epacket_serial_prefix_search(state, buffer[i]);
		if (state->pkt_idx <= 4) {
			continue;
		}

		/* Allocate RX buffer */
		if (state->pkt_idx == 5) {
			if (state->payload_remaining > CONFIG_EPACKET_PACKET_SIZE_MAX) {
				LOG_WRN("Payload %d too large", state->payload_remaining);
			} else {
				/* Can't block in interrupts */
				state->rx_buffer = epacket_alloc_rx(K_NO_WAIT);
				if (state->rx_buffer == NULL) {
					LOG_WRN("Dropping packet");
				}
			}
		}
		/* Payload bytes */
		uint16_t to_add = MIN(state->payload_remaining, len - i);

		/* No more data in provided buffer */
		if (to_add == 0) {
			return;
		}

		/* Add payload to buffer */
		if (state->rx_buffer) {
			net_buf_add_mem(state->rx_buffer, buffer + i,
					MIN(to_add, net_buf_tailroom(state->rx_buffer)));
		}
		state->pkt_idx += to_add;
		state->payload_remaining -= to_add;
		i += to_add - 1;

		/* Still more bytes to come after exhausting this input buffer */
		if (state->payload_remaining) {
			return;
		}

		/* All data received, add metadata and ship it */
		if (state->rx_buffer) {
			meta = net_buf_user_data(state->rx_buffer);
			meta->interface = dev;
			meta->interface_id = EPACKET_INTERFACE_SERIAL;
			meta->rx_timestamp = k_uptime_ticks();
			meta->rssi = 0;

			/* Hand off to core ePacket functions */
			handler(state->rx_buffer);
		}
		/* Reset parsing state */
		state->rx_buffer = NULL;
		state->pkt_idx = 0;
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
