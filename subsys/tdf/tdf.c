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

#include "diffs.h"

#define INT24_MAX 0x7FFFFF
#define INT24_MIN ((-INT24_MAX) - 1)

struct tdf_header {
	uint16_t id_flags;
	uint8_t size;
} __packed;

struct tdf_array_header {
	union {
		uint8_t num;
		uint8_t diff_info;
	};
	union {
		/**
		 * When the TDF_ARRAY_TIME_PERIOD_SCALED bit is set, the
		 * TDF_ARRAY_TIME_PERIOD_VAL_MASK value is scaled by
		 * TDF_ARRAY_TIME_SCALE_FACTOR
		 */
		uint16_t period;
		/** Index of the first sample in the array for TDF_ARRAY_IDX */
		uint16_t sample_num;
	};
} __packed;

enum tdf_diff_type {
	TDF_DIFF_NONE = 0,
	/** 16 bit data, 8 bit diffs */
	TDF_DIFF_16_8 = 1,
	/** 32 bit data, 8 bit diffs */
	TDF_DIFF_32_8 = 2,
	/** 32 bit data, 16 bit diffs */
	TDF_DIFF_32_16 = 3,
} __packed;

#define TDF_MAXIMUM_DIFFS 64

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

static uint8_t tdf_diff_divisor[] = {
	[TDF_DATA_FORMAT_DIFF_ARRAY_16_8] = sizeof(uint16_t),
	[TDF_DATA_FORMAT_DIFF_ARRAY_32_8] = sizeof(uint32_t),
	[TDF_DATA_FORMAT_DIFF_ARRAY_32_16] = sizeof(uint32_t),

};
static uint8_t tdf_diff_size[] = {
	[TDF_DATA_FORMAT_DIFF_ARRAY_16_8] = sizeof(int8_t),
	[TDF_DATA_FORMAT_DIFF_ARRAY_32_8] = sizeof(int8_t),
	[TDF_DATA_FORMAT_DIFF_ARRAY_32_16] = sizeof(int16_t),
};

static uint32_t sign_extend_24_bits(uint32_t x)
{
	const int bits = 24;
	uint32_t m = 1u << (bits - 1);

	return (x ^ m) - m;
}

#ifdef CONFIG_TDF_DIFF

static enum tdf_diff_type tdf_diff_encoded[] = {
	[TDF_DATA_FORMAT_DIFF_ARRAY_16_8] = TDF_DIFF_16_8,
	[TDF_DATA_FORMAT_DIFF_ARRAY_32_8] = TDF_DIFF_32_8,
	[TDF_DATA_FORMAT_DIFF_ARRAY_32_16] = TDF_DIFF_32_16,
};

static tdf_diff_check_t tdf_diff_check_functions[] = {
	[TDF_DATA_FORMAT_DIFF_ARRAY_16_8] = tdf_diff_check_16_8,
	[TDF_DATA_FORMAT_DIFF_ARRAY_32_8] = tdf_diff_check_32_8,
	[TDF_DATA_FORMAT_DIFF_ARRAY_32_16] = tdf_diff_check_32_16,

};
static tdf_diff_encode_t tdf_diff_encode_functions[] = {
	[TDF_DATA_FORMAT_DIFF_ARRAY_16_8] = tdf_diff_encode_16_8,
	[TDF_DATA_FORMAT_DIFF_ARRAY_32_8] = tdf_diff_encode_32_8,
	[TDF_DATA_FORMAT_DIFF_ARRAY_32_16] = tdf_diff_encode_32_16,
};
static tdf_diff_apply_t tdf_diff_apply_functions[] = {
	[TDF_DATA_FORMAT_DIFF_ARRAY_16_8] = tdf_diff_apply_16_8,
	[TDF_DATA_FORMAT_DIFF_ARRAY_32_8] = tdf_diff_apply_32_8,
	[TDF_DATA_FORMAT_DIFF_ARRAY_32_16] = tdf_diff_apply_32_16,
};

static int tdf_num_valid_diffs(enum tdf_data_format diff_type, uint8_t tdf_len, uint8_t tdf_num,
			       const void *data)
{
	tdf_diff_check_t check_fn = tdf_diff_check_functions[diff_type];
	const uint8_t *p0 = (const uint8_t *)data;
	const uint8_t *p1 = p0 + tdf_len;
	int i;

	__ASSERT_NO_MSG(tdf_num > 2);

	/* Find the first valid diff */
	for (i = 0; i < (tdf_num - 2); i++) {
		if (check_fn(tdf_len, p0, p1)) {
			/* First diff valid, check the second */
			p0 += tdf_len;
			p1 += tdf_len;
			if (check_fn(tdf_len, p0, p1)) {
				/* Second diff valid */
				p0 += tdf_len;
				p1 += tdf_len;
				break;
			}
			i += 1;
		}
		p0 += tdf_len;
		p1 += tdf_len;
	}
	/* If the first valid diff is at 0, find how many diffs exist */
	if (i == 0) {
		/* A single array can only have so many diffs */
		tdf_num = MIN(tdf_num, TDF_MAXIMUM_DIFFS);
		/* Scan until we find an invalid diff */
		for (i = 2; i < (tdf_num - 1); i++) {
			if (!check_fn(tdf_len, p0, p1)) {
				break;
			}
			p0 += tdf_len;
			p1 += tdf_len;
		}
		return i;
	} else if (i == (tdf_num - 2)) {
		/* No valid diffs found */
		return -(i + 2);
	}
	/* Return the number of elements until the valid diff */
	return -i;
}

#endif /* CONFIG_TDF_DIFF */

static int tdf_input_validate(uint16_t tdf_id, uint8_t tdf_len, uint8_t tdf_num,
			      uint32_t idx_period, enum tdf_data_format format, uint16_t min_size,
			      uint16_t max_space)
{
	/* Invalid TDF ID or idx_period */
	if ((tdf_id == 0) || (tdf_id >= 4095) || (tdf_len == 0) || (tdf_num == 0) ||
	    (format >= TDF_DATA_FORMAT_INVALID)) {
		return -EINVAL;
	}
	/* Invalid idx_period */
	if ((format != TDF_DATA_FORMAT_IDX_ARRAY) && (idx_period > TDF_ARRAY_TIME_PERIOD_MAX)) {
		return -EINVAL;
	}
	/* TDF can never fit on the buffer */
	if (min_size > max_space) {
		return -ENOSPC;
	}
	return 0;
}

static struct tdf_header *tdf_add_header(struct tdf_buffer_state *state, uint16_t tdf_header,
					 uint16_t tdf_id, uint8_t tdf_len, int64_t epoch_time,
					 int32_t timestamp_delta)
{
	struct tdf_header *header = net_buf_simple_add(&state->buf, sizeof(struct tdf_header));

	header->id_flags = tdf_header | tdf_id;
	header->size = tdf_len;

	switch (tdf_header) {
	case TDF_TIMESTAMP_RELATIVE:
		net_buf_simple_add_le16(&state->buf, timestamp_delta);
		state->time = epoch_time;
		break;
	case TDF_TIMESTAMP_EXTENDED_RELATIVE:
		net_buf_simple_add_le24(&state->buf, timestamp_delta);
		state->time = epoch_time;
		break;
	case TDF_TIMESTAMP_ABSOLUTE:
		struct tdf_time *t = net_buf_simple_add(&state->buf, sizeof(struct tdf_time));

		t->seconds = epoch_time_seconds(epoch_time);
		t->subseconds = epoch_time_subseconds(epoch_time);
		state->time = epoch_time;
		break;
	default:
		break;
	}

	return header;
}

static struct tdf_array_header *tdf_add_array_header(struct tdf_buffer_state *state,
						     struct tdf_header *header, uint8_t tdf_num,
						     uint32_t idx_period, bool is_idx, bool is_diff)
{
	static struct tdf_array_header *array_header_ptr;

	if ((tdf_num <= 1) && !is_idx) {
		/* No header needed */
		return NULL;
	}

	/* Add array header */
	array_header_ptr = net_buf_simple_add(&state->buf, sizeof(struct tdf_array_header));

	if (is_idx) {
		header->id_flags |= TDF_ARRAY_IDX;
		/* Start sample index saved directly. 16 bit rollover is fine */
		array_header_ptr->sample_num = (uint16_t)idx_period;
	} else {
		if (is_diff) {
			header->id_flags |= TDF_ARRAY_DIFF;
		} else {
			header->id_flags |= TDF_ARRAY_TIME;
		}
		if (idx_period > TDF_ARRAY_TIME_PERIOD_VAL_MASK) {
			array_header_ptr->period = TDF_ARRAY_TIME_PERIOD_SCALED |
						   (idx_period / TDF_ARRAY_TIME_SCALE_FACTOR);
		} else {
			array_header_ptr->period = idx_period;
		}
	}
	array_header_ptr->num = tdf_num;
	return array_header_ptr;
}

int tdf_add_core(struct tdf_buffer_state *state, uint16_t tdf_id, uint8_t tdf_len, uint8_t tdf_num,
		 uint64_t time, uint32_t idx_period, const void *data, enum tdf_data_format format)
{
	struct tdf_array_header *array_header_ptr;
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
	bool is_idx = format == TDF_DATA_FORMAT_IDX_ARRAY;
	bool is_diff = false;
	int rc;

#ifdef CONFIG_TDF_DIFF
	uint8_t per_tdf_fields = 0;
	uint8_t per_tdf_diff_size = 1;
	bool diff_precomputed = !!(format & TDF_DATA_FORMAT_DIFF_PRECOMPUTED);

	format &= ~TDF_DATA_FORMAT_DIFF_PRECOMPUTED;
	is_diff = (format == TDF_DATA_FORMAT_DIFF_ARRAY_16_8) ||
		  (format == TDF_DATA_FORMAT_DIFF_ARRAY_32_8) ||
		  (format == TDF_DATA_FORMAT_DIFF_ARRAY_32_16);
#else
	/* Override requested diff */
	if (!is_idx) {
		format = TDF_DATA_FORMAT_TIME_ARRAY;
	}
#endif /* CONFIG_TDF_DIFF */

	/* Input validation */
	rc = tdf_input_validate(tdf_id, tdf_len, tdf_num, idx_period, format, min_size, max_space);
	if (rc < 0) {
		return rc;
	}

	/* Evaluate header sizes assuming all data will fit */
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
#ifdef CONFIG_TDF_DIFF
	if (is_diff && (tdf_len % tdf_diff_divisor[format] != 0)) {
		/* TDF length not a multiple of the diff */
		return -EINVAL;
	}
	if (is_diff && tdf_num > 2) {
		if (!diff_precomputed) {
			/* Require that at least 2 valid diffs in a row exist to log as
			 * TDF_ARRAY_DIFF. Otherwise, log up to that point as a normal
			 * TDF_ARRAY_TIME
			 */
			int diffs = tdf_num_valid_diffs(format, tdf_len, tdf_num, data);

			if (diffs < 0) {
				/* No diff encoding */
				format = TDF_DATA_FORMAT_TIME_ARRAY;
				is_diff = false;
				tdf_num = -diffs;
			} else {
				tdf_num = 1 + diffs;
			}
		}
	} else {
		format = TDF_DATA_FORMAT_SINGLE;
		is_diff = false;
	}
#endif /* CONFIG_TDF_DIFF */
	if ((tdf_num > 1) || is_idx) {
		array_header = sizeof(struct tdf_array_header);
	} else {
		format = TDF_DATA_FORMAT_SINGLE;
		is_diff = false;
	}
	total_header = sizeof(struct tdf_header) + array_header + timestamp_header;
	/* Validate we have some room for payload */
	if (buffer_remaining <= total_header) {
		return -ENOMEM;
	}

	/* Can complete payload fit? */
	if (!is_diff) {
		total_data = (uint16_t)tdf_len * tdf_num;
	}
#ifdef CONFIG_TDF_DIFF
	else {
		per_tdf_fields = tdf_len / tdf_diff_divisor[format];
		per_tdf_diff_size = per_tdf_fields * tdf_diff_size[format];
		total_data = (uint16_t)tdf_len + ((tdf_num - 1) * per_tdf_diff_size);
	}
#endif /* CONFIG_TDF_DIFF */

	payload_space = buffer_remaining - total_header;

	if (payload_space < total_data) {
		/* Evaluate how many TDF payloads can fit */
		uint8_t can_fit = 0;

		if (!is_diff) {
			can_fit = payload_space / tdf_len;
		}
#ifdef CONFIG_TDF_DIFF
		else if (payload_space >= tdf_len) {
			can_fit = 1 + ((payload_space - tdf_len) / per_tdf_diff_size);
		}
#endif /* CONFIG_TDF_DIFF */

		/* Header may contain bytes we don't need */
		if ((can_fit == 0) && (tdf_num > 1) && !is_idx) {
			/* Reclaim the time array header space and re-evaluate */
			payload_space += sizeof(struct tdf_array_header);
			can_fit = payload_space / tdf_len;
		}
		if (can_fit == 0) {
			return -ENOMEM;
		}
		tdf_num = can_fit;
		if (!is_diff) {
			total_data = (uint16_t)tdf_len * tdf_num;
		}
#ifdef CONFIG_TDF_DIFF
		else {
			total_data = (uint16_t)tdf_len + ((tdf_num - 1) * per_tdf_diff_size);
		}
#endif /* CONFIG_TDF_DIFF */
	}

	/* Base Headers */
	struct tdf_header *header =
		tdf_add_header(state, tdf_header, tdf_id, tdf_len, time, timestamp_delta);

	/* Array headers (optional) */
	array_header_ptr =
		tdf_add_array_header(state, header, tdf_num, idx_period, is_idx, is_diff);

#ifdef CONFIG_TDF_DIFF
	if (is_diff && tdf_num > 1) {
		int total_diff_size = (tdf_num - 1) * per_tdf_diff_size;

		if (diff_precomputed) {
			/* Add the base TDF */
			net_buf_simple_add_mem(&state->buf, data, tdf_len + total_diff_size);
		} else {
			tdf_diff_encode_t encode_fn = tdf_diff_encode_functions[format];
			const uint8_t *current = data;
			const uint8_t *next = current + tdf_len;
			void *diff_ptr;

			/* Add the base TDF */
			net_buf_simple_add_mem(&state->buf, data, tdf_len);

			/* Add all the diff data.
			 * Note that we have already limited the size to 32 and
			 * validated that the diffs are valid.
			 */
			diff_ptr = net_buf_simple_add(&state->buf, total_diff_size);

			encode_fn((tdf_num - 1) * per_tdf_fields, current, next, diff_ptr);
		}
		array_header_ptr->diff_info = (tdf_diff_encoded[format] << 6) | (tdf_num - 1);
	} else {
#endif /* CONFIG_TDF_DIFF */
		/* Add TDF data */
		net_buf_simple_add_mem(&state->buf, data, total_data);
#ifdef CONFIG_TDF_DIFF
	}
#endif /* CONFIG_TDF_DIFF */

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
	struct tdf_array_header *t_hdr;
	uint8_t diff_bytes_per_tdf;
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
		if (state->buf.len <= sizeof(struct tdf_array_header)) {
			return -EINVAL;
		}
		t_hdr = net_buf_simple_pull_mem(&state->buf, sizeof(struct tdf_array_header));

		if (array_flags == TDF_ARRAY_DIFF) {
			enum tdf_diff_type dt = (t_hdr->diff_info >> 6);

			parsed->data_type = TDF_DATA_FORMAT_DIFF_ARRAY_16_8 + dt - 1;
			parsed->diff_info.num = t_hdr->diff_info & 0x3F;

			if (dt == TDF_DIFF_NONE) {
				/* Corrupt buffer */
				return -EINVAL;
			}
			diff_bytes_per_tdf = (header->size / tdf_diff_divisor[parsed->data_type]) *
					     tdf_diff_size[parsed->data_type];
			data_len = (uint16_t)parsed->tdf_len +
				   (parsed->diff_info.num * diff_bytes_per_tdf);
		} else if (array_flags == TDF_ARRAY_TIME) {
			parsed->data_type = TDF_DATA_FORMAT_TIME_ARRAY;
			parsed->tdf_num = t_hdr->num;
			data_len = (uint16_t)parsed->tdf_len * parsed->tdf_num;
		} else if (array_flags == TDF_ARRAY_IDX) {
			parsed->data_type = TDF_DATA_FORMAT_IDX_ARRAY;
			parsed->tdf_num = t_hdr->num;
			data_len = (uint16_t)parsed->tdf_len * parsed->tdf_num;
		} else {
			return -EINVAL;
		}

		if (array_flags == TDF_ARRAY_IDX) {
			parsed->base_idx = t_hdr->sample_num;
		} else {
			if (t_hdr->period & TDF_ARRAY_TIME_PERIOD_SCALED) {
				parsed->period = TDF_ARRAY_TIME_SCALE_FACTOR *
						 (t_hdr->period & TDF_ARRAY_TIME_PERIOD_VAL_MASK);
			} else {
				parsed->period = t_hdr->period;
			}
		}
	} else {
		parsed->data_type = TDF_DATA_FORMAT_SINGLE;
		data_len = (uint16_t)parsed->tdf_len;
	}
	if (state->buf.len < data_len) {
		return -EINVAL;
	}
	parsed->data = net_buf_simple_pull_mem(&state->buf, data_len);
	return 0;
}

#ifdef CONFIG_TDF_DIFF

int tdf_parse_diff_reconstruct(const struct tdf_parsed *parsed, void *output, uint8_t idx)
{
	tdf_diff_apply_t apply_fn;
	uint8_t *diff_ptr = (uint8_t *)parsed->data + parsed->tdf_len;
	uint8_t per_tdf_fields, per_tdf_diff_size;
	const enum tdf_data_format format = parsed->data_type;
	bool is_diff = (format == TDF_DATA_FORMAT_DIFF_ARRAY_16_8) ||
		       (format == TDF_DATA_FORMAT_DIFF_ARRAY_32_8) ||
		       (format == TDF_DATA_FORMAT_DIFF_ARRAY_32_16);

	if (!is_diff || (idx > parsed->diff_info.num)) {
		return -EINVAL;
	}

	apply_fn = tdf_diff_apply_functions[format];
	per_tdf_fields = parsed->tdf_len / tdf_diff_divisor[format];
	per_tdf_diff_size = per_tdf_fields * tdf_diff_size[format];

	/* Setup base output */
	memcpy(output, parsed->data, parsed->tdf_len);

	/* Sequentially apply diffs */
	for (int i = 0; i < idx; i++) {
		apply_fn(parsed->tdf_len, output, output, diff_ptr);
		diff_ptr += per_tdf_diff_size;
	}
	return 0;
}

#endif /* CONFIG_TDF_DIFF */
