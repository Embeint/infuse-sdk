/**
 * @file
 * @copyright 2024 Embeint Inc
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

#include <infuse/security.h>
#include <infuse/identifiers.h>
#include <infuse/time/civil.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/keys.h>
#include <infuse/epacket/interface/epacket_udp.h>

#include "epacket_internal.h"

int epacket_udp_encrypt(struct net_buf *buf)
{
	struct epacket_tx_metadata *meta = net_buf_user_data(buf);
	uint64_t civil_time = civil_time_seconds(civil_time_now());
	uint64_t device_id = infuse_device_id();
	struct epacket_udp_frame *frame;
	uint16_t buf_len = buf->len;
	struct net_buf *scratch;
	uint32_t key_identifier;
	psa_key_id_t psa_key_id;
	psa_status_t status;
	size_t out_len;
	uint8_t key_id;

	static uint16_t sequence_num;

	/* Validate space for frame header */
	__ASSERT_NO_MSG(net_buf_headroom(buf) >= sizeof(struct epacket_udp_frame));

	/* Only device auth used for direct UDP comms */
	if (meta->auth == EPACKET_AUTH_NETWORK) {
		meta->auth = EPACKET_KEY_DEVICE;
	}

	key_id = EPACKET_KEY_DEVICE | EPACKET_KEY_INTERFACE_UDP;
	meta->flags |= EPACKET_FLAGS_ENCRYPTION_DEVICE;
	key_identifier = infuse_security_device_key_identifier();

	/* Get the PSA key ID for packet */
	psa_key_id = epacket_key_id_get(key_id, civil_time / SECONDS_PER_DAY);
	if (psa_key_id == 0) {
		return -1;
	}

	/* Push buffer */
	frame = net_buf_push(buf, sizeof(struct epacket_udp_frame));
	*frame = (struct epacket_udp_frame){
		.associated_data =
			{
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
	sys_put_le24(key_identifier, frame->associated_data.key_identifier);

	/* Claim scratch space as encryption cannot be applied in place */
	scratch = epacket_encryption_scratch();
	__ASSERT_NO_MSG(net_buf_tailroom(scratch) >= buf->len);

	/* Move plaintext across to scratch space */
	net_buf_add_mem(scratch, net_buf_remove_mem(buf, buf_len), buf_len);

	/* Encrypt back into the original buffer */
	status = psa_aead_encrypt(psa_key_id, PSA_ALG_CHACHA20_POLY1305, frame->nonce.raw,
				  sizeof(frame->nonce.raw), frame->associated_data.raw,
				  sizeof(frame->associated_data), scratch->data, scratch->len,
				  net_buf_tail(buf), net_buf_tailroom(buf), &out_len);
	net_buf_add(buf, out_len);

	/* Free scratch space */
	net_buf_unref(scratch);

	return status == PSA_SUCCESS ? 0 : -1;
}

int epacket_udp_decrypt(struct net_buf *buf)
{
	struct epacket_rx_metadata *meta = net_buf_user_data(buf);
	struct epacket_udp_frame frame;
	struct net_buf *scratch;
	uint64_t device_id;
	uint32_t key_identifier;
	psa_key_id_t psa_key_id;
	uint8_t epacket_key_id;
	psa_status_t status;
	size_t out_len;

	/* Not enough data in buffer */
	if (buf->len <= sizeof(struct epacket_udp_frame) + 16) {
		return -1;
	}
	memcpy(&frame, buf->data, sizeof(frame));

	/* Packet metadata */
	meta->type = frame.associated_data.type;
	meta->flags = frame.associated_data.flags;
	meta->auth = EPACKET_AUTH_DEVICE;
	meta->sequence = frame.nonce.sequence;
	key_identifier = sys_get_le24(frame.associated_data.key_identifier);

	/* Validate packet should be for us and device encrypted */
	device_id = ((uint64_t)frame.associated_data.device_id_upper << 32) |
		    frame.nonce.device_id_lower;
	if ((device_id != infuse_device_id()) ||
	    !(frame.associated_data.flags & EPACKET_FLAGS_ENCRYPTION_DEVICE)) {
		goto error;
	}
	if (key_identifier != infuse_security_device_key_identifier()) {
		goto error;
	}

	/* Get the PSA key ID for packet */
	epacket_key_id = EPACKET_KEY_DEVICE | EPACKET_KEY_INTERFACE_UDP;
	psa_key_id = epacket_key_id_get(epacket_key_id, frame.nonce.gps_time / SECONDS_PER_DAY);
	if (psa_key_id == 0) {
		goto error;
	}

	/* Claim scratch space as decryption cannot be applied in place */
	scratch = epacket_encryption_scratch();

	/* Copy ciphertext across to scratch space */
	net_buf_pull(buf, sizeof(struct epacket_udp_frame));
	net_buf_add_mem(scratch, buf->data, buf->len);
	net_buf_reset(buf);

	/* Decrypt into the allocated packet buffer */
	status = psa_aead_decrypt(psa_key_id, PSA_ALG_CHACHA20_POLY1305, frame.nonce.raw,
				  sizeof(frame.nonce), frame.associated_data.raw,
				  sizeof(frame.associated_data), scratch->data, scratch->len,
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
