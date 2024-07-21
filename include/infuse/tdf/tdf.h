/**
 * @file
 * @brief TDF payload generation
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 *
 * @details
 * Tagged Data Format payload generation.
 * Evolution of the data logging format described in:
 *   https://doi.org/10.1007/978-3-319-03071-5_2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TDF_TDF_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TDF_TDF_H_

#include <stdint.h>

#include <zephyr/net/buf.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief TDF API
 * @defgroup tdf_apis TDF APIs
 * @{
 */

struct tdf_buffer_state {
	/* Current buffer time */
	uint64_t time;
	/* Buffer information */
	struct net_buf_simple buf;
};

struct tdf_parsed {
	/* TDF time (0 for none) */
	uint64_t time;
	/* TDF ID */
	uint16_t tdf_id;
	/* Length of single TDF */
	uint8_t tdf_len;
	/* Number of TDFs */
	uint8_t tdf_num;
	/* Time period between TDFs */
	uint16_t period;
	/* TDF data */
	void *data;
};

enum tdf_flags {
	/* Timestamp flags */
	TDF_TIMESTAMP_NONE = 0x0000,
	TDF_TIMESTAMP_ABSOLUTE = 0x4000,
	TDF_TIMESTAMP_RELATIVE = 0x8000,
	TDF_TIMESTAMP_EXTENDED_RELATIVE = 0xC000,
	/* Special flags */
	TDF_TIME_ARRAY = 0x1000,
	/* Masks */
	TDF_FLAGS_MASK = 0xF000,
	TDF_TIMESTAMP_MASK = 0xC000,
	TDF_ID_MASK = 0x0FFF,
};

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
 * @retval -ENOSPC TDF too large to ever fit on buffer
 * @return -ENOMEM Insufficient space to add any TDFs to buffer
 */
int tdf_add(struct tdf_buffer_state *state, uint16_t tdf_id, uint8_t tdf_len, uint8_t tdf_num,
	    uint64_t time, uint16_t period, const void *data);

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
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TDF_TDF_H_ */
