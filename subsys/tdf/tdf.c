/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <string.h>

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

static int tdf_first_valid_diff_index(uint8_t tdf_len, uint8_t tdf_num, const void *data,
				      tdf_diff_encode_t diff_encode)
{
	const uint8_t *p0 = (const uint8_t *)data;
	const uint8_t *p1 = p0 + tdf_len;

	for (int i = 0; i < (tdf_num - 2); i++) {
		if (diff_encode(p0, p1, NULL)) {
			/* First diff valid, check the second */
			p0 += tdf_len;
			p1 += tdf_len;

			if (diff_encode(p0, p1, NULL)) {
				/* Second diff valid */
				return i;
			}

			/* Already checked the second diff */
			i += 1;
		}
		p0 += tdf_len;
		p1 += tdf_len;
	}
	/* No valids diffs on buffer */
	return -1;
}

int tdf_add_diff(struct tdf_buffer_state *state, uint16_t tdf_id, uint8_t tdf_len, uint8_t tdf_num,
		 uint64_t time, uint32_t period, const void *data, uint8_t num_fields,
		 tdf_diff_encode_t diff_encode)
{
	struct tdf_time_array_header *array_header_ptr = NULL;
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
	if (diff_encode && tdf_num > 2) {
		/* Require that at least 2 valid diffs in a row exist to log as TDF_ARRAY_DIFF.
		 * Otherwise, log up to that point as a normal TDF_ARRAY_TIME
		 */
		int first_index = tdf_first_valid_diff_index(tdf_len, tdf_num, data, diff_encode);

		if (first_index != 0) {
			/* No diff encoding */
			diff_encode = NULL;
			if (first_index > 0) {
				tdf_num = first_index;
			}
		}
	} else {
		diff_encode = NULL;
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
	if (diff_encode == NULL) {
		total_data = (uint16_t)tdf_len * tdf_num;
	} else {
		__ASSERT_NO_MSG(num_fields != 0);
		/* Header num refers to total number of diff bytes, which is limited to UINT8_MAX */
		tdf_num = MIN(tdf_num, UINT8_MAX / num_fields);
		total_data = (uint16_t)tdf_len + ((tdf_num - 1) * num_fields);
	}

	payload_space = buffer_remaining - total_header;

	if (payload_space < total_data) {
		/* Evaluate how many TDF payloads can fit */
		uint8_t can_fit = 0;

		if (diff_encode == NULL) {
			can_fit = payload_space / tdf_len;
		} else if (payload_space >= tdf_len) {
			can_fit = 1 + ((payload_space - tdf_len) / num_fields);
		}

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
		if (diff_encode == NULL) {
			total_data = (uint16_t)tdf_len * tdf_num;
		} else {
			total_data = (uint16_t)tdf_len + ((tdf_num - 1) * num_fields);
		}
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
		array_header_ptr =
			net_buf_simple_add(&state->buf, sizeof(struct tdf_time_array_header));

		header->id_flags |= diff_encode ? TDF_ARRAY_DIFF : TDF_ARRAY_TIME;
		array_header_ptr->num = tdf_num;
		if (period > TDF_ARRAY_TIME_PERIOD_VAL_MASK) {
			array_header_ptr->period = TDF_ARRAY_TIME_PERIOD_SCALED |
						   (period / TDF_ARRAY_TIME_SCALE_FACTOR);
		} else {
			array_header_ptr->period = period;
		}
	}

	if (diff_encode && tdf_num > 1) {
		const uint8_t *current = data;
		const uint8_t *next = current + tdf_len;
		uint8_t *diff_ptr;

		/* Add the base TDF */
		net_buf_simple_add_mem(&state->buf, data, tdf_len);

		/* Add diffs until we run out of TDFs or a diff doesn't fit */
		for (int i = 0; i < (tdf_num - 1); i++) {
			diff_ptr = net_buf_simple_add(&state->buf, num_fields);
			if (!diff_encode(current, next, diff_ptr)) {
				/* Remove pending diff */
				net_buf_simple_remove_mem(&state->buf, num_fields);
				/* Update how many TDFs we handled */
				tdf_num = 1 + i;
				array_header_ptr->num = tdf_num;
				break;
			}
			current += tdf_len;
			next += tdf_len;
		}
		array_header_ptr->num = (tdf_num - 1) * num_fields;
	} else {
		/* Add TDF data */
		net_buf_simple_add_mem(&state->buf, data, total_data);
	}

	return tdf_num;
}

int tdf_parse(struct tdf_buffer_state *state, struct tdf_parsed *parsed)
{
	if (state->buf.len <= sizeof(struct tdf_header)) {
		return -ENOMEM;
	}

	struct tdf_header *header = net_buf_simple_pull_mem(&state->buf, sizeof(struct tdf_header));
	uint16_t time_flags = header->id_flags & TDF_TIMESTAMP_MASK;
	uint16_t array_flags = header->id_flags & TDF_ARRAY_MASK;
	struct tdf_time_array_header *t_hdr;
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
	switch (time_flags) {
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

	switch (time_flags) {
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
	if (array_flags) {
		if (array_flags == TDF_ARRAY_DIFF) {
			parsed->data_type = TDF_DATA_TYPE_DIFF_ARRAY;
		} else if (array_flags == TDF_ARRAY_TIME) {
			parsed->data_type = TDF_DATA_TYPE_TIME_ARRAY;
		} else {
			return -EINVAL;
		}
		if (state->buf.len <= sizeof(struct tdf_time_array_header)) {
			return -EINVAL;
		}
		t_hdr = net_buf_simple_pull_mem(&state->buf, sizeof(struct tdf_time_array_header));

		parsed->tdf_num = t_hdr->num;
		if (t_hdr->period & TDF_ARRAY_TIME_PERIOD_SCALED) {
			parsed->period = TDF_ARRAY_TIME_SCALE_FACTOR *
					 (t_hdr->period & TDF_ARRAY_TIME_PERIOD_VAL_MASK);
		} else {
			parsed->period = t_hdr->period;
		}
	} else {
		parsed->data_type = TDF_DATA_TYPE_SINGLE;
	}
	if (array_flags == TDF_ARRAY_DIFF) {
		data_len = (uint16_t)parsed->tdf_len + parsed->diff_num;
	} else {
		data_len = (uint16_t)parsed->tdf_len * parsed->tdf_num;
	}
	if (state->buf.len < data_len) {
		return -EINVAL;
	}
	parsed->data = net_buf_simple_pull_mem(&state->buf, data_len);
	return 0;
}
