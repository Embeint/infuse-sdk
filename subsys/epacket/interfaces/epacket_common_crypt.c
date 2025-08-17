/**
 * @file
 * @copyright 2024 Embeint Inc
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

#include <psa/crypto.h>

#include <infuse/identifiers.h>
#include <infuse/security.h>
#include <infuse/time/epoch.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/keys.h>
#include <infuse/epacket/interface/common.h>

#include "epacket_internal.h"

LOG_MODULE_DECLARE(epacket);

int epacket_versioned_v0_encrypt(struct net_buf *buf, uint8_t interface_key,
				 uint32_t network_key_id)
{
	struct epacket_tx_metadata *meta = net_buf_user_data(buf);
	uint64_t epoch_time = epoch_time_seconds(epoch_time_now());
	uint64_t device_id = infuse_device_id();
	struct epacket_v0_versioned_frame_format *frame;
	uint16_t buf_len = buf->len;
	struct net_buf *scratch;
	uint32_t key_identifier;
	psa_key_id_t psa_key_id;
	uint8_t epacket_key_id;
	psa_status_t status;
	size_t out_len;

	static uint16_t sequence_num;

	/* Packet was already encrypted by third-party */
	if (meta->auth == EPACKET_AUTH_REMOTE_ENCRYPTED) {
		return 0;
	}

	/* Validate space for frame header */
	__ASSERT_NO_MSG(net_buf_headroom(buf) >= sizeof(struct epacket_v0_versioned_frame_format));

	if (meta->auth == EPACKET_AUTH_NETWORK) {
		epacket_key_id = EPACKET_KEY_NETWORK | interface_key;
		meta->flags |= EPACKET_FLAGS_ENCRYPTION_NETWORK;
		key_identifier = network_key_id;
	} else {
		epacket_key_id = EPACKET_KEY_DEVICE | interface_key;
		meta->flags |= EPACKET_FLAGS_ENCRYPTION_DEVICE;
		key_identifier = infuse_security_device_key_identifier();
	}

	/* Get the PSA key ID for packet */
	psa_key_id =
		epacket_key_id_get(epacket_key_id, key_identifier, epoch_time / SECONDS_PER_DAY);
	if (psa_key_id == PSA_KEY_ID_NULL) {
		return -1;
	}

	/* Push buffer */
	frame = net_buf_push(buf, sizeof(struct epacket_v0_versioned_frame_format));
	*frame = (struct epacket_v0_versioned_frame_format){
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
				.gps_time = epoch_time,
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

	meta->sequence = frame->nonce.sequence;
	return status == PSA_SUCCESS ? 0 : -1;
}

int epacket_versioned_v0_decrypt(struct net_buf *buf, uint8_t interface_key)
{
	struct epacket_rx_metadata *meta = net_buf_user_data(buf);
	struct epacket_v0_versioned_frame_format frame;
	struct net_buf *scratch;
	psa_key_id_t psa_key_id;
	uint8_t epacket_key_id;
	psa_status_t status;
	size_t out_len;

	/* Not enough data in buffer */
	if (buf->len <= sizeof(struct epacket_v0_versioned_frame_format) + 16) {
		goto error;
	}
	memcpy(&frame, buf->data, sizeof(frame));

	/* Not V0 */
	if (frame.associated_data.version != 0) {
		goto error;
	}

	/* Packet metadata */
	meta->type = frame.associated_data.type;
	meta->flags = frame.associated_data.flags;
	meta->sequence = frame.nonce.sequence;
	meta->key_identifier = sys_get_le24(frame.associated_data.key_identifier);
	meta->packet_gps_time = frame.nonce.gps_time;
	meta->packet_device_id = ((uint64_t)frame.associated_data.device_id_upper << 32) |
				 frame.nonce.device_id_lower;

	if (frame.associated_data.flags & EPACKET_FLAGS_ENCRYPTION_DEVICE) {
		meta->auth = EPACKET_AUTH_DEVICE;
		/* Validate packet is for us */
		if (meta->packet_device_id != infuse_device_id()) {
			goto error;
		}
		epacket_key_id = EPACKET_KEY_DEVICE | interface_key;
	} else {
		meta->auth = EPACKET_AUTH_NETWORK;
		epacket_key_id = EPACKET_KEY_NETWORK | interface_key;
	}

	/* Get the PSA key ID for packet */
	psa_key_id = epacket_key_id_get(epacket_key_id, meta->key_identifier,
					frame.nonce.gps_time / SECONDS_PER_DAY);
	if (psa_key_id == PSA_KEY_ID_NULL) {
		goto error;
	}

	/* Claim scratch space as decryption cannot be applied in place */
	scratch = epacket_encryption_scratch();

	/* Copy ciphertext across to scratch space */
	net_buf_pull(buf, sizeof(struct epacket_v0_versioned_frame_format));
	net_buf_add_mem(scratch, buf->data, buf->len);
	net_buf_reset(buf);

	/* Decrypt back into original buffer */
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

int epacket_unversioned_v0_encrypt(struct net_buf *buf, uint8_t interface_key,
				   uint32_t network_key_id)
{
	struct epacket_tx_metadata *meta = net_buf_user_data(buf);
	uint64_t epoch_time = epoch_time_seconds(epoch_time_now());
	uint64_t device_id = infuse_device_id();
	struct epacket_v0_unversioned_frame_format *frame;
	uint16_t buf_len = buf->len;
	struct net_buf *scratch;
	uint32_t key_identifier;
	psa_key_id_t psa_key_id;
	uint8_t epacket_key_id;
	psa_status_t status;
	size_t out_len;

	static uint16_t sequence_num;

	/* Packet was already encrypted by third-party */
	if (meta->auth == EPACKET_AUTH_REMOTE_ENCRYPTED) {
		return 0;
	}

	/* Validate space for frame header */
	__ASSERT_NO_MSG(net_buf_headroom(buf) >=
			sizeof(struct epacket_v0_unversioned_frame_format));

	/* Only device auth used for direct UDP comms */
	if (meta->auth == EPACKET_AUTH_NETWORK) {
		epacket_key_id = EPACKET_KEY_NETWORK | interface_key;
		meta->flags |= EPACKET_FLAGS_ENCRYPTION_NETWORK;
		key_identifier = network_key_id;
	} else {
		epacket_key_id = EPACKET_KEY_DEVICE | interface_key;
		meta->flags |= EPACKET_FLAGS_ENCRYPTION_DEVICE;
		key_identifier = infuse_security_device_key_identifier();
	}

	/* Get the PSA key ID for packet */
	psa_key_id =
		epacket_key_id_get(epacket_key_id, key_identifier, epoch_time / SECONDS_PER_DAY);
	if (psa_key_id == PSA_KEY_ID_NULL) {
		return -1;
	}

	/* Push buffer */
	frame = net_buf_push(buf, sizeof(struct epacket_v0_unversioned_frame_format));
	*frame = (struct epacket_v0_unversioned_frame_format){
		.associated_data =
			{
				.type = meta->type,
				.flags = meta->flags,
				.device_id_upper = (device_id >> 32),
			},
		.nonce =
			{
				.device_id_lower = device_id & UINT32_MAX,
				.gps_time = epoch_time,
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

	meta->sequence = frame->nonce.sequence;
	return status == PSA_SUCCESS ? 0 : -1;
}

int epacket_unversioned_v0_decrypt(struct net_buf *buf, uint8_t interface_key)
{
	struct epacket_rx_metadata *meta = net_buf_user_data(buf);
	struct epacket_v0_unversioned_frame_format frame;
	struct net_buf *scratch;
	psa_key_id_t psa_key_id;
	uint8_t epacket_key_id;
	psa_status_t status;
	size_t out_len;

	/* Not enough data in buffer */
	if (buf->len <= sizeof(struct epacket_v0_unversioned_frame_format) + 16) {
		goto error;
	}
	memcpy(&frame, buf->data, sizeof(frame));

	/* Packet metadata */
	meta->type = frame.associated_data.type;
	meta->flags = frame.associated_data.flags;
	meta->auth = EPACKET_AUTH_DEVICE;
	meta->sequence = frame.nonce.sequence;
	meta->key_identifier = sys_get_le24(frame.associated_data.key_identifier);
	meta->packet_gps_time = frame.nonce.gps_time;
	meta->packet_device_id = ((uint64_t)frame.associated_data.device_id_upper << 32) |
				 frame.nonce.device_id_lower;

	if (frame.associated_data.flags & EPACKET_FLAGS_ENCRYPTION_DEVICE) {
		meta->auth = EPACKET_AUTH_DEVICE;
		/* Validate packet is for us */
		if (meta->packet_device_id != infuse_device_id()) {
			goto error;
		}
		epacket_key_id = EPACKET_KEY_DEVICE | interface_key;
	} else {
		meta->auth = EPACKET_AUTH_NETWORK;
		epacket_key_id = EPACKET_KEY_NETWORK | interface_key;
	}

	/* Get the PSA key ID for packet */
	psa_key_id = epacket_key_id_get(epacket_key_id, meta->key_identifier,
					frame.nonce.gps_time / SECONDS_PER_DAY);
	if (psa_key_id == PSA_KEY_ID_NULL) {
		goto error;
	}

	/* Claim scratch space as decryption cannot be applied in place */
	scratch = epacket_encryption_scratch();

	/* Copy ciphertext across to scratch space */
	net_buf_pull(buf, sizeof(struct epacket_v0_unversioned_frame_format));
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

int epacket_unversioned_v0_tx_decrypt(struct net_buf *buf, uint8_t interface_key)
{
	struct epacket_v0_unversioned_frame_format *frame;
	psa_key_id_t psa_key_id;
	struct net_buf *scratch;
	uint8_t epacket_key_id;
	psa_status_t status;
	size_t out_len;

	/* Claim scratch space as decryption cannot be applied in place */
	scratch = epacket_encryption_scratch();

	/* Copy ciphertext across to scratch space */
	net_buf_add_mem(scratch, buf->data, buf->len);
	net_buf_reset(buf);

	frame = (void *)scratch->data;

	if (frame->associated_data.flags & EPACKET_FLAGS_ENCRYPTION_DEVICE) {
		epacket_key_id = EPACKET_KEY_DEVICE | interface_key;
	} else {
		epacket_key_id = EPACKET_KEY_NETWORK | interface_key;
	}

	/* Get the PSA key ID for packet */
	psa_key_id = epacket_key_id_get(epacket_key_id,
					sys_get_le24(frame->associated_data.key_identifier),
					frame->nonce.gps_time / SECONDS_PER_DAY);
	if (psa_key_id == PSA_KEY_ID_NULL) {
		goto error;
	}

	/* Decrypt back into the original packet buffer */
	status = psa_aead_decrypt(
		psa_key_id, PSA_ALG_CHACHA20_POLY1305, frame->nonce.raw, sizeof(frame->nonce),
		frame->associated_data.raw, sizeof(frame->associated_data), frame->ciphertext_tag,
		scratch->len - sizeof(*frame), net_buf_tail(buf), net_buf_tailroom(buf), &out_len);

	if (status != PSA_SUCCESS) {
		/* Restore original buffer */
		net_buf_add_mem(buf, &frame, sizeof(frame));
		net_buf_add_mem(buf, scratch->data, scratch->len);
		goto error;
	}
	net_buf_add(buf, out_len);
	net_buf_unref(scratch);
	return 0;
error:
	net_buf_unref(scratch);
	return -1;
}
