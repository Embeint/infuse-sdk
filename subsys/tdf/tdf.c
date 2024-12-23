/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/kernel.h>

#include <infuse/tdf/tdf.h>
#include <infuse/time/epoch.h>

#define INT24_MAX 0x7FFFFF
#define INT24_MIN ((-INT24_MAX) - 1)

struct tdf_header {
	uint16_t id_flags;
	uint8_t size;
} __packed;

struct tdf_time_array_header {
	uint8_t num;
	/**
	 * When the TDF_ARRAY_TIME_PERIOD_SCALED bit is set, the
	 * TDF_ARRAY_TIME_PERIOD_VAL_MASK value is scaled by
	 * TDF_ARRAY_TIME_SCALE_FACTOR
	 */
	uint16_t period;
} __packed;

/* Bit that signifies the value is scaled by a multiplier */
#define TDF_ARRAY_TIME_PERIOD_SCALED   0x8000
/* Mask of the period value */
#define TDF_ARRAY_TIME_PERIOD_VAL_MASK 0x7FFF
/* Gives a time resolution of 125 ms (8192 / 65536) */
#define TDF_ARRAY_TIME_SCALE_FACTOR    8192

#define TDF_ARRAY_TIME_PERIOD_MAX (TDF_ARRAY_TIME_PERIOD_VAL_MASK * TDF_ARRAY_TIME_SCALE_FACTOR)

struct tdf_time {
	uint32_t seconds;
	uint16_t subseconds;
} __packed;

static uint32_t sign_extend_24_bits(uint32_t x)
{
	const int bits = 24;
	uint32_t m = 1u << (bits - 1);

	return (x ^ m) - m;
}

int tdf_add(struct tdf_buffer_state *state, uint16_t tdf_id, uint8_t tdf_len, uint8_t tdf_num,
	    uint64_t time, uint32_t period, const void *data)
{
	uint16_t buffer_remaining = net_buf_simple_tailroom(&state->buf);
	uint16_t max_space = state->buf.size - (state->buf.data - state->buf.__buf);
	uint16_t min_size =
		sizeof(struct tdf_header) + (time ? sizeof(struct tdf_time) : 0) + tdf_len;
	uint16_t payload_space;
	uint16_t total_header, total_data;
	uint16_t array_header = 0;
	uint16_t timestamp_header = 0;
	int64_t timestamp_delta = 0;
	uint16_t tdf_header = TDF_TIMESTAMP_NONE;

	/* Invalid TDF ID or period */
	if ((tdf_id == 0) || (tdf_id >= 4095) || (tdf_len == 0) || (tdf_num == 0) ||
	    (period > TDF_ARRAY_TIME_PERIOD_MAX)) {
		return -EINVAL;
	}
	/* TDF can never fit on the buffer */
	if (min_size > max_space) {
		return -ENOSPC;
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

		t->seconds = epoch_time_seconds(time);
		t->subseconds = epoch_time_subseconds(time);
		state->time = time;
		break;
	default:
		break;
	}
	/* Add array header */
	if (tdf_num > 1) {
		struct tdf_time_array_header *t =
			net_buf_simple_add(&state->buf, sizeof(struct tdf_time_array_header));

		header->id_flags |= TDF_ARRAY_TIME;
		t->num = tdf_num;

		if (period > TDF_ARRAY_TIME_PERIOD_VAL_MASK) {
			t->period = TDF_ARRAY_TIME_PERIOD_SCALED |
				    (period / TDF_ARRAY_TIME_SCALE_FACTOR);
		} else {
			t->period = period;
		}
	}

	/* Add TDF data */
	net_buf_simple_add_mem(&state->buf, data, total_data);

	return tdf_num;
}

int tdf_parse(struct tdf_buffer_state *state, struct tdf_parsed *parsed)
{
	if (state->buf.len <= sizeof(struct tdf_header)) {
		return -ENOMEM;
	}

	struct tdf_header *header = net_buf_simple_pull_mem(&state->buf, sizeof(struct tdf_header));
	uint16_t flags = header->id_flags & TDF_FLAGS_MASK;
	uint16_t data_len;
	int32_t time_diff;

	parsed->tdf_id = header->id_flags & TDF_ID_MASK;
	parsed->tdf_len = header->size;
	parsed->tdf_num = 1;
	parsed->period = 0;

	/* Invalid TDF ID */
	if ((parsed->tdf_id == 0) || (parsed->tdf_id == 4095)) {
		return -EINVAL;
	}

	/* Validate header length */
	switch (flags & TDF_TIMESTAMP_MASK) {
	case TDF_TIMESTAMP_ABSOLUTE:
		data_len = sizeof(struct tdf_time);
		break;
	case TDF_TIMESTAMP_RELATIVE:
		data_len = sizeof(uint16_t);
		break;
	case TDF_TIMESTAMP_EXTENDED_RELATIVE:
		data_len = 3;
		break;
	default:
		data_len = 0;
	}
	if (state->buf.len <= data_len) {
		return -EINVAL;
	}

	switch (flags & TDF_TIMESTAMP_MASK) {
	case TDF_TIMESTAMP_ABSOLUTE:
		struct tdf_time *t = net_buf_simple_pull_mem(&state->buf, sizeof(struct tdf_time));

		state->time = epoch_time_from(t->seconds, t->subseconds);
		parsed->time = state->time;
		break;
	case TDF_TIMESTAMP_RELATIVE:
		time_diff = net_buf_simple_pull_le16(&state->buf);

		if (state->time == 0) {
			return -EINVAL;
		}
		state->time += time_diff;
		parsed->time = state->time;
		break;
	case TDF_TIMESTAMP_EXTENDED_RELATIVE:
		time_diff = net_buf_simple_pull_le24(&state->buf);
		time_diff = sign_extend_24_bits(time_diff);

		if (state->time == 0) {
			return -EINVAL;
		}
		state->time += time_diff;
		parsed->time = state->time;
		break;
	default:
		parsed->time = 0;
		break;
	}
	if (flags & TDF_ARRAY_TIME) {
		if (state->buf.len <= sizeof(struct tdf_time_array_header)) {
			return -EINVAL;
		}
		struct tdf_time_array_header *t =
			net_buf_simple_pull_mem(&state->buf, sizeof(struct tdf_time_array_header));

		parsed->tdf_num = t->num;
		if (t->period & TDF_ARRAY_TIME_PERIOD_SCALED) {
			parsed->period = TDF_ARRAY_TIME_SCALE_FACTOR *
					 (t->period & TDF_ARRAY_TIME_PERIOD_VAL_MASK);
		} else {
			parsed->period = t->period;
		}
	}
	data_len = (uint16_t)parsed->tdf_len * parsed->tdf_num;
	if (state->buf.len < data_len) {
		return -EINVAL;
	}
	parsed->data = net_buf_simple_pull_mem(&state->buf, data_len);
	return 0;
}
