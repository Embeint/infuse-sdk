/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <string.h>
#include <stdint.h>

#include <zephyr/random/random.h>
#include <zephyr/net/buf.h>
#include <zephyr/sys/byteorder.h>

#include <psa/crypto.h>

#include <infuse/identifiers.h>
#include <infuse/time/civil.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/keys.h>
#include <infuse/epacket/interface/epacket_serial.h>

#include "epacket_internal.h"

static const uint8_t sync_bytes[2] = {SERIAL_SYNC_A, SERIAL_SYNC_B};

/* Validate frame sizes match expected */
BUILD_ASSERT(sizeof(struct epacket_serial_frame) == EPACKET_SERIAL_FRAME_EXPECTED_SIZE, "USB frame changed size");

void epacket_serial_reconstruct(const struct device *dev, uint8_t *buffer, size_t len,
				void (*handler)(struct net_buf *))
{
	static struct net_buf *rx_buffer;
	static uint16_t payload_remaining;
	static uint8_t len_lsb;
	static uint16_t header_idx;
	struct epacket_rx_metadata *meta;

	for (int i = 0; i < len; i++) {
		switch (header_idx) {
		case 0:
		case 1:
			if (buffer[i] != sync_bytes[header_idx]) {
				header_idx = 0;
				continue;
			}
			break;
		case 2:
			len_lsb = buffer[i];
			break;
		case 3:
			payload_remaining = ((uint16_t)buffer[i] << 8) | len_lsb;
			if (payload_remaining > CONFIG_EPACKET_PACKET_SIZE_MAX) {
				/* Can't receive this packet */
				header_idx = 0;
			}
			break;
		}
		/* Still waiting on the payload length */
		if (header_idx <= 3) {
			header_idx++;
			continue;
		}
		/* Claim memory if none */
		if (rx_buffer == NULL) {
			rx_buffer = epacket_alloc_rx(K_FOREVER);
		}
		/* Payload bytes */
		uint16_t to_add = MIN(payload_remaining, len - i);

		/* Add payload to buffer */
		net_buf_add_mem(rx_buffer, buffer + i, to_add);
		payload_remaining -= to_add;
		i += to_add - 1;

		/* Is packet done? */
		if (payload_remaining == 0) {
			meta = net_buf_user_data(rx_buffer);
			meta->interface = dev;
			meta->interface_id = EPACKET_INTERFACE_SERIAL;
			meta->interface_id = 0;

			/* Hand off to core ePacket functions */
			handler(rx_buffer);
			/* Reset parsing state */
			rx_buffer = NULL;
			header_idx = 0;
		}
	}
}

int epacket_serial_encrypt(struct net_buf *buf)
{
	struct epacket_tx_metadata *meta = net_buf_user_data(buf);
	uint64_t civil_time = civil_time_seconds(civil_time_now());
	uint64_t device_id = infuse_device_id();
	struct epacket_serial_frame *frame;
	uint16_t buf_len = buf->len;
	struct net_buf *scratch;
	uint32_t key_rotation, key_meta;
	psa_key_id_t psa_key_id;
	psa_status_t status;
	size_t out_len;
	uint8_t key_id;

	static uint16_t sequence_num;

	/* Validate space for frame header */
	__ASSERT_NO_MSG(net_buf_headroom(buf) >= sizeof(struct epacket_serial_frame));

	if (meta->auth == EPACKET_AUTH_NETWORK) {
		key_id = EPACKET_KEY_NETWORK | EPACKET_KEY_INTERFACE_SERIAL;
		meta->flags |= EPACKET_FLAGS_ENCRYPTION_NETWORK;
		meta->flags |= EPACKET_FLAGS_ROTATE_NETWORK_EACH_MINUTE;
		key_meta = epacket_network_key_id();
		/* TODO: increase rotation period once cloud handles it correctly */
		key_rotation = civil_time / SECONDS_PER_MINUTE;
	} else {
		key_id = EPACKET_KEY_DEVICE | EPACKET_KEY_INTERFACE_SERIAL;
		meta->flags |= EPACKET_FLAGS_ENCRYPTION_DEVICE;
		key_meta = 1;
		key_rotation = 1;
	}

	/* Get the PSA key ID for packet */
	psa_key_id = epacket_key_id_get(key_id, key_rotation);
	if (psa_key_id == 0) {
		return -1;
	}

	/* Push buffer */
	frame = net_buf_push(buf, sizeof(struct epacket_serial_frame));
	*frame = (struct epacket_serial_frame){
		.associated_data =
			{
				.version = 0,
				.type = meta->type,
				.flags = meta->flags,
				.device_id_upper = (device_id >> 32),
			},
		.nonce =
			{
				.device_id_lower = device_id & UINT32_MAX,
				.gps_time = civil_time,
				.sequence = sequence_num++,
				.entropy = sys_rand32_get(),
			},
	};
	sys_put_le24(key_meta, frame->associated_data.device_rotation);

	/* Claim scratch space as encryption cannot be applied in place */
	scratch = epacket_encryption_scratch();
	__ASSERT_NO_MSG(net_buf_tailroom(scratch) >= buf->len);

	/* Move plaintext across to scratch space */
	net_buf_add_mem(scratch, net_buf_remove_mem(buf, buf_len), buf_len);

	/* Encrypt back into the original buffer */
	status = psa_aead_encrypt(psa_key_id, PSA_ALG_CHACHA20_POLY1305, frame->nonce.raw, sizeof(frame->nonce.raw),
				  frame->associated_data.raw, sizeof(frame->associated_data), scratch->data,
				  scratch->len, net_buf_tail(buf), net_buf_tailroom(buf), &out_len);
	net_buf_add(buf, out_len);

	/* Free scratch space */
	net_buf_unref(scratch);

	return status == PSA_SUCCESS ? 0 : -1;
}

int epacket_serial_decrypt(struct net_buf *buf)
{
	struct epacket_rx_metadata *meta = net_buf_user_data(buf);
	struct epacket_serial_frame frame;
	struct net_buf *scratch;
	uint32_t key_rotation, key_period;
	uint32_t network_id;
	uint64_t device_id;
	uint8_t key_id;
	psa_key_id_t psa_key_id;
	psa_status_t status;
	size_t out_len;

	/* Not enough data in buffer */
	if (buf->len <= sizeof(struct epacket_serial_frame)) {
		return -1;
	}
	memcpy(&frame, buf->data, sizeof(frame));

	/* Packet metadata */
	meta->type = frame.associated_data.type;
	meta->flags = frame.associated_data.flags;
	meta->sequence = frame.nonce.sequence;

	if (frame.associated_data.flags & EPACKET_FLAGS_ENCRYPTION_DEVICE) {
		meta->auth = EPACKET_AUTH_DEVICE;
		/* Validate packet is for us */
		device_id = ((uint64_t)frame.associated_data.device_id_upper << 32) | frame.nonce.device_id_lower;
		if (device_id != infuse_device_id()) {
			goto error;
		}
		key_id = EPACKET_KEY_DEVICE | EPACKET_KEY_INTERFACE_SERIAL;
		key_rotation = sys_get_le24(frame.associated_data.device_rotation);
	} else {
		meta->auth = EPACKET_AUTH_NETWORK;
		/* Validate the network IDs match */
		network_id = sys_get_le24(frame.associated_data.network_id);
		if (network_id != epacket_network_key_id()) {
			goto error;
		}
		key_id = EPACKET_KEY_NETWORK | EPACKET_KEY_INTERFACE_SERIAL;
		key_rotation = (frame.associated_data.flags & EPACKET_FLAGS_ROTATE_NETWORK_MASK) >> 12;
		switch (key_rotation) {
		case EPACKET_FLAGS_ROTATE_NETWORK_EACH_MINUTE:
			key_period = SECONDS_PER_MINUTE;
			break;
		case EPACKET_FLAGS_ROTATE_NETWORK_EACH_HOUR:
			key_period = SECONDS_PER_HOUR;
			break;
		case EPACKET_FLAGS_ROTATE_NETWORK_EACH_DAY:
			key_period = SECONDS_PER_DAY;
			break;
		case EPACKET_FLAGS_ROTATE_NETWORK_EACH_WEEK:
		default:
			key_period = SECONDS_PER_WEEK;
			break;
		}
		key_rotation = frame.nonce.gps_time / key_period;
	}

	/* Get the PSA key ID for packet */
	psa_key_id = epacket_key_id_get(key_id, key_rotation);
	if (psa_key_id == 0) {
		goto error;
	}

	/* Claim scratch space as decryption cannot be applied in place */
	scratch = epacket_encryption_scratch();

	/* Copy ciphertext across to scratch space */
	net_buf_pull(buf, sizeof(struct epacket_serial_frame));
	net_buf_add_mem(scratch, buf->data, buf->len);
	net_buf_reset(buf);

	/* Decrypt back into original buffer */
	status = psa_aead_decrypt(psa_key_id, PSA_ALG_CHACHA20_POLY1305, frame.nonce.raw, sizeof(frame.nonce),
				  frame.associated_data.raw, sizeof(frame.associated_data), scratch->data, scratch->len,
				  net_buf_tail(buf), net_buf_tailroom(buf), &out_len);

	if (status != PSA_SUCCESS) {
		/* Restore original buffer */
		net_buf_add_mem(buf, &frame, sizeof(frame));
		net_buf_add_mem(buf, scratch->data, scratch->len);
		net_buf_unref(scratch);
		goto error;
	}
	net_buf_add(buf, out_len);
	net_buf_unref(scratch);
	return 0;
error:
	meta->auth = EPACKET_AUTH_FAILURE;
	return -1;
}
