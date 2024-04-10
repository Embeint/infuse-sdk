/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/kernel.h>

#include <eis/tdf/tdf.h>
#include <eis/time/civil.h>

#define INT24_MAX 0x7FFFFF
#define INT24_MIN ((-INT24_MAX) - 1)

struct tdf_header {
	uint16_t id_flags;
	uint8_t size;
} __packed;

struct tdf_time_array_header {
	uint8_t num;
	uint16_t period;
} __packed;

struct tdf_time {
	uint32_t seconds;
	uint16_t subseconds;
} __packed;

int tdf_add(uint16_t tdf_id, uint8_t tdf_len, uint8_t tdf_num, uint64_t time, uint16_t period,
	    struct tdf_buffer_state *state, const void *data)
{
	uint16_t buffer_remaining = net_buf_simple_tailroom(&state->buf);
	uint16_t payload_space;
	uint16_t total_header, total_data;
	uint16_t array_header = 0;
	uint16_t timestamp_header = 0;
	int64_t timestamp_delta = 0;
	uint16_t tdf_header = TDF_TIMESTAMP_NONE;

	/* Invalid TDF ID */
	if ((tdf_id == 0) || (tdf_id >= 4095) || (tdf_len == 0) || (tdf_num == 0)) {
		return -EINVAL;
	}

	/* Evaluate headers sizes assuming all data will fit */
	if (time) {
		if (state->time) {
			timestamp_delta = time - state->time;

			if (IN_RANGE(timestamp_delta, 0, UINT16_MAX)) {
				timestamp_header = sizeof(uint16_t);
				tdf_header = TDF_TIMESTAMP_RELATIVE;
			} else if (IN_RANGE(timestamp_delta, INT24_MIN, INT24_MAX)) {
				timestamp_header = 3;
				tdf_header = TDF_TIMESTAMP_EXTENDED_RELATIVE;
			} else {
				timestamp_header = sizeof(struct tdf_time);
				tdf_header = TDF_TIMESTAMP_ABSOLUTE;
			}
		} else {
			timestamp_header = sizeof(struct tdf_time);
			tdf_header = TDF_TIMESTAMP_ABSOLUTE;
		}
	}
	if (tdf_num > 1) {
		array_header = sizeof(struct tdf_time_array_header);
	}
	total_header = sizeof(struct tdf_header) + array_header + timestamp_header;
	/* Validate we have some room for payload */
	if (buffer_remaining <= total_header) {
		return -ENOMEM;
	}

	/* Can complete payload fit? */
	total_data = (uint16_t)tdf_len * tdf_num;
	payload_space = buffer_remaining - total_header;

	if (payload_space < total_data) {
		/* Evaluate how many TDF payloads can fit */
		uint8_t can_fit = payload_space / tdf_len;

		/* Header may contain bytes we don't need */
		if ((can_fit == 0) && (tdf_num > 1)) {
			/* Reclaim the time array header space and re-evaluate */
			payload_space += sizeof(struct tdf_time_array_header);
			can_fit = payload_space / tdf_len;
		}
		if (can_fit == 0) {
			return -ENOMEM;
		}
		tdf_num = can_fit;
		total_data = (uint16_t)tdf_len * tdf_num;
	}

	/* Log data */
	struct tdf_header *header = net_buf_simple_add(&state->buf, sizeof(struct tdf_header));

	header->id_flags = tdf_header | tdf_id;
	header->size = tdf_len;

	switch (tdf_header) {
	case TDF_TIMESTAMP_RELATIVE:
		net_buf_simple_add_le16(&state->buf, timestamp_delta);
		state->time = time;
		break;
	case TDF_TIMESTAMP_EXTENDED_RELATIVE:
		net_buf_simple_add_le24(&state->buf, timestamp_delta);
		state->time = time;
		break;
	case TDF_TIMESTAMP_ABSOLUTE:
		struct tdf_time *t = net_buf_simple_add(&state->buf, sizeof(struct tdf_time));

		t->seconds = civil_time_seconds(time);
		t->subseconds = civil_time_subseconds(time);
		state->time = time;
		break;
	default:
		break;
	}
	/* Add array header */
	if (tdf_num > 1) {
		struct tdf_time_array_header *t = net_buf_simple_add(&state->buf, sizeof(struct tdf_time_array_header));

		header->id_flags |= TDF_TIME_ARRAY;
		t->num = tdf_num;
		t->period = period;
	}

	/* Add TDF data */
	net_buf_simple_add_mem(&state->buf, data, total_data);

	return tdf_num;
}
