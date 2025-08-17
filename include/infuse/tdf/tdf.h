/**
 * @file
 * @brief TDF payload generation
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 * Tagged Data Format payload generation.
 * Evolution of the data logging format described in:
 *   https://doi.org/10.1007/978-3-319-03071-5_2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TDF_TDF_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TDF_TDF_H_

#include <stdint.h>

#include <zephyr/net_buf.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Tagged Data Format API
 * @defgroup tdf_apis Tagged Data Format APIs
 * @{
 */

struct tdf_buffer_state {
	/** Current buffer time */
	uint64_t time;
	/** Buffer information */
	struct net_buf_simple buf;
};

enum tdf_data_format {
	/** Single sample */
	TDF_DATA_FORMAT_SINGLE,
	/** Time array with period */
	TDF_DATA_FORMAT_TIME_ARRAY,
	/** Array based on sample indicies */
	TDF_DATA_FORMAT_IDX_ARRAY,
	/** 16 bit data, 8 bit diffs */
	TDF_DATA_FORMAT_DIFF_ARRAY_16_8,
	/** 32 bit data, 8 bit diffs */
	TDF_DATA_FORMAT_DIFF_ARRAY_32_8,
	/** 32 bit data, 16 bit diffs */
	TDF_DATA_FORMAT_DIFF_ARRAY_32_16,
	/** Start of invalid range */
	TDF_DATA_FORMAT_INVALID,
	/** Data is already in [base, diff...] form */
	TDF_DATA_FORMAT_DIFF_PRECOMPUTED = 0x80,
} __packed;

struct tdf_parsed {
	/** TDF time (0 for none) */
	uint64_t time;
	/** TDF ID */
	uint16_t tdf_id;
	/** Length of single TDF */
	uint8_t tdf_len;
	/** Data format */
	enum tdf_data_format data_type;
	union {
		/** Number of TDFs */
		uint8_t tdf_num;
		struct {
			/** Number of diffs */
			uint8_t num;
		} diff_info;
	};
	union {
		/** Time period between TDFs */
		uint32_t period;
		/** Index of first sample for @ref TDF_DATA_FORMAT_IDX_ARRAY */
		uint16_t base_idx;
	};
	/** TDF data */
	void *data;
};

enum tdf_flags {
	/** Timestamp flags */
	TDF_TIMESTAMP_NONE = 0x0000,
	TDF_TIMESTAMP_ABSOLUTE = 0x4000,
	TDF_TIMESTAMP_RELATIVE = 0x8000,
	TDF_TIMESTAMP_EXTENDED_RELATIVE = 0xC000,
	/** Special flags */
	TDF_ARRAY_NONE = 0x0000,
	TDF_ARRAY_TIME = 0x1000,
	TDF_ARRAY_DIFF = 0x2000,
	TDF_ARRAY_IDX = 0x3000,
	/** Masks */
	TDF_FLAGS_MASK = 0xF000,
	TDF_TIMESTAMP_MASK = 0xC000,
	TDF_ARRAY_MASK = 0x3000,
	TDF_ID_MASK = 0x0FFF,
};

/**
 * @brief Get type associated with a given TDF ID
 *
 * @param tdf_id TDF sensor ID
 *
 * @retval Associated tdf struct type
 */
#define TDF_TYPE(tdf_id) _##tdf_id##_TYPE

/**
 * @brief Type safe wrapper around @ref tdf_add
 *
 * Adds compile-time validation that the passed pointer matches the type associated
 * with @a tdf_id.
 *
 * @note Only works for TDF types without trailing variable length arrays
 *
 * @param state Pointer to current buffer state
 * @param tdf_id TDF sensor ID
 * @param tdf_num Number of TDFs to try to add
 * @param base_time Epoch time associated with the first TDF. 0 for no timestamp.
 * @param period Epoch time between tdfs when @a tdf_num > 0.
 * @param data TDF data
 *
 * @retval >0 Number of TDFs successfully added to buffer
 * @retval -EINVAL Invalid arguments
 * @retval -ENOSPC TDF too large to ever fit on buffer
 * @retval -ENOMEM Insufficient space to add any TDFs to buffer
 */
#define TDF_ADD(state, tdf_id, tdf_num, base_time, period, data)                                   \
	tdf_add(state, tdf_id, sizeof(TDF_TYPE(tdf_id)), tdf_num, base_time, period, data);        \
	do {                                                                                       \
		__maybe_unused const TDF_TYPE(tdf_id) *_data = data;                               \
	} while (0)

/**
 * @brief Reset a tdf_buffer_state struct
 *
 * @param state struct to reset
 */
static inline void tdf_buffer_state_reset(struct tdf_buffer_state *state)
{
	net_buf_simple_reset(&state->buf);
	state->time = 0;
}

/**
 * @brief Add TDFs to memory buffer with an explicit format
 *
 * @param state Pointer to current buffer state
 * @param tdf_id TDF sensor ID
 * @param tdf_len Length of a single TDF
 * @param tdf_num Number of TDFs to try to add
 * @param time Epoch time associated with the first TDF. 0 for no timestamp.
 * @param idx_period Index of first sample if @a format == TDF_DATA_FORMAT_IDX_ARRAY
 *                   Epoch time between tdfs when @a tdf_num > 0 otherwise.
 * @param data TDF data
 * @param format Data encoding format
 *
 * @retval >0 Number of TDFs successfully added to buffer
 * @retval -EINVAL Invalid arguments
 * @retval -ENOSPC TDF too large to ever fit on buffer
 * @retval -ENOMEM Insufficient space to add any TDFs to buffer
 */
int tdf_add_core(struct tdf_buffer_state *state, uint16_t tdf_id, uint8_t tdf_len, uint8_t tdf_num,
		 uint64_t time, uint32_t idx_period, const void *data, enum tdf_data_format format);

/**
 * @brief Add TDFs to memory buffer
 *
 * @param state Pointer to current buffer state
 * @param tdf_id TDF sensor ID
 * @param tdf_len Length of a single TDF
 * @param tdf_num Number of TDFs to try to add
 * @param time Epoch time associated with the first TDF. 0 for no timestamp.
 * @param period Epoch time between tdfs when @a tdf_num > 0.
 * @param data TDF data
 *
 * @retval >0 Number of TDFs successfully added to buffer
 * @retval -EINVAL Invalid arguments
 * @retval -ENOSPC TDF too large to ever fit on buffer
 * @retval -ENOMEM Insufficient space to add any TDFs to buffer
 */
static inline int tdf_add(struct tdf_buffer_state *state, uint16_t tdf_id, uint8_t tdf_len,
			  uint8_t tdf_num, uint64_t time, uint32_t period, const void *data)
{
	enum tdf_data_format format =
		tdf_num > 1 ? TDF_DATA_FORMAT_TIME_ARRAY : TDF_DATA_FORMAT_SINGLE;

	return tdf_add_core(state, tdf_id, tdf_len, tdf_num, time, period, data, format);
}

/**
 * @brief Initialise TDF parsing state
 *
 * @param state State to initialise
 * @param data Pointer to TDF memory buffer
 * @param size Size of TDF memory buffer
 */
static inline void tdf_parse_start(struct tdf_buffer_state *state, void *data, size_t size)
{
	net_buf_simple_init_with_data(&state->buf, data, size);
	state->time = 0;
}

/**
 * @brief Parse the next TDF from a memory buffer
 *
 * @param state TDF parsing state
 * @param parsed Pointer to output TDF information
 *
 * @retval 0 on successful parse
 * @retval -EINVAL on invalid TDF
 * @retval -ENOMEM on no more TDFs
 */
int tdf_parse(struct tdf_buffer_state *state, struct tdf_parsed *parsed);

/**
 * @brief Find the first instance of a specific TDF in a memory buffer
 *
 * @param data Pointer to TDF memory buffer
 * @param size Size of TDF memory buffer
 * @param tdf_id TDF ID to search for
 * @param parsed Storage for TDF info
 *
 * @retval 0 On success
 * @retval -ENOMEM If buffer consumed without finding the TDF
 */
static inline int tdf_parse_find_in_buf(void *data, size_t size, uint16_t tdf_id,
					struct tdf_parsed *parsed)
{
	struct tdf_buffer_state state;

	tdf_parse_start(&state, data, size);
	while (true) {
		if (tdf_parse(&state, parsed) < 0) {
			return -ENOMEM;
		}
		if (parsed->tdf_id == tdf_id) {
			return 0;
		}
	}
	return -ENOMEM;
}

/**
 * @brief Reconstruct the original TDF from a parsed @ref TDF_ARRAY_DIFF
 *
 * @param parsed Parsed TDF from @ref tdf_parse
 * @param output Output location to construct TDF into
 * @param idx Index of the TDF to reconstruct (0 == base, 1 == apply diff[0], etc)
 *
 * @retval 0 On success
 * @retval -EINVAL On invalid argument
 */
int tdf_parse_diff_reconstruct(const struct tdf_parsed *parsed, void *output, uint8_t idx);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TDF_TDF_H_ */
