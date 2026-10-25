/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>

#include "patch_internal.h"

enum patch_opcode {
	COPY_LEN_U4 = 0 << 4,
	COPY_LEN_U12 = 1 << 4,
	COPY_LEN_U20 = 2 << 4,
	COPY_LEN_U32 = 3 << 4,
	WRITE_LEN_U4 = 4 << 4,
	WRITE_LEN_U12 = 5 << 4,
	WRITE_LEN_U20 = 6 << 4,
	WRITE_LEN_U32 = 7 << 4,
	ADDR_SHIFT_S8 = 8 << 4,
	ADDR_SHIFT_S16 = 9 << 4,
	ADDR_SET_U32 = 10 << 4,
	PATCH = 11 << 4,
	PATCH_DELTA = 12 << 4,
	OPCODE_MASK = 0xF0,
	DATA_MASK = 0x0F,
};

enum opcode_family {
	FAMILY_NONE = 0,
	FAMILY_COPY = 1,
	FAMILY_WRITE = 2,
	FAMILY_PATCH = 3,
	FAMILY_ADDR = 4,
	FAMILY_PATCH_DELTA = 5,
};

/* PATCH_DELTA element addition values */
#define DELTA_HISTORY_SIZE 14
#define DELTA_EXPLICIT     14
#define DELTA_NONE         15

struct patch_state {
	struct cpatch_ctx *ctx;
	enum opcode_family pending;
	uint32_t operation_count;
	/* Most recently used PATCH_DELTA deltas, persists across the patch */
	uint16_t delta_history[DELTA_HISTORY_SIZE];
};

static const uint16_t delta_history_initial[DELTA_HISTORY_SIZE] = {
	1, 0xFFFF, 2, 0xFFFE, 3, 0xFFFD, 4, 0xFFFC, 5, 0xFFFB, 6, 0xFFFA, 7, 0xFFF9,
};

LOG_MODULE_DECLARE(binary_patch, CONFIG_CPATCH_LOG_LEVEL);

static int do_copy(struct patch_state *state)
{
	int rc = cpatch_input_copy(state->ctx, state->operation_count);

	state->operation_count = 0;
	return rc;
}

static int do_write(struct patch_state *state)
{
	int rc = cpatch_patch_write(state->ctx, state->operation_count);

	state->operation_count = 0;
	return rc;
}

static int do_cpatch(struct patch_state *state)
{
	int rc;

	while (true) {
		uint8_t len;

		rc = cpatch_patch_read(state->ctx, &len, 1);
		if (len == 0) {
			break;
		}
		state->operation_count = len & 0x7F;
		LOG_DBG("PATCH_COPY: %d", state->operation_count);
		rc = do_copy(state);
		if (rc < 0) {
			return rc;
		}
		if (len & 0x80) {
			state->operation_count = 1;
		} else {
			rc = cpatch_patch_read(state->ctx, &len, 1);
			if (len == 0) {
				break;
			}
			state->operation_count = len;
		}
		LOG_DBG("PATCH_WRITE: %d", state->operation_count);
		rc = do_write(state);
		if (rc < 0) {
			return rc;
		}
	}

	return 0;
}

static uint16_t delta_history_use(struct patch_state *state, uint8_t index, uint16_t delta)
{
	/* Explicit deltas evict the least recently used entry */
	if (index != DELTA_EXPLICIT) {
		delta = state->delta_history[index];
	} else {
		index = DELTA_HISTORY_SIZE - 1;
	}
	/* Move the delta to the front of the history */
	memmove(&state->delta_history[1], &state->delta_history[0], index * sizeof(uint16_t));
	state->delta_history[0] = delta;
	return delta;
}

static int do_cpatch_delta(struct patch_state *state)
{
	struct cpatch_ctx *ctx = state->ctx;
	uint8_t header, addition;
	uint16_t delta = 0;
	int rc;

	while (true) {
		rc = cpatch_patch_read(ctx, &header, 1);
		if (rc < 0) {
			return rc;
		}
		if (header == DELTA_NONE) {
			/* COPY of 0 with no addition terminates the instruction */
			break;
		}
		state->operation_count = header >> 4;
		addition = header & DATA_MASK;
		if (state->operation_count == 15) {
			rc = cpatch_patch_read(ctx, ctx->buffer, 1);
			if (rc < 0) {
				return rc;
			}
			state->operation_count += ctx->buffer[0];
		}
		if (addition == DELTA_EXPLICIT) {
			rc = cpatch_patch_read(ctx, ctx->buffer, 2);
			if (rc < 0) {
				return rc;
			}
			delta = sys_get_le16(ctx->buffer);
		}
		LOG_DBG("PATCH_DELTA_COPY: %d", state->operation_count);
		rc = do_copy(state);
		if (rc < 0) {
			return rc;
		}
		if (addition == DELTA_NONE) {
			continue;
		}
		delta = delta_history_use(state, addition, delta);
		LOG_DBG("PATCH_DELTA_ADD: %04X", delta);

		/* Add delta to the next halfword of the input */
		rc = cpatch_input_read(ctx, ctx->input_offset, ctx->buffer, 2);
		if (rc < 0) {
			return rc;
		}
		sys_put_le16(sys_get_le16(ctx->buffer) + delta, ctx->buffer);
		rc = stream_flash_buffered_write(ctx->output, ctx->buffer, 2, false);
		if (rc < 0) {
			return rc;
		}
		ctx->input_offset += 2;
	}

	return 0;
}

static int opcode_fetch(struct patch_state *state)
{
	struct cpatch_ctx *ctx = state->ctx;
	uint8_t op_code, op_data;
	int rc;

	rc = cpatch_patch_read(ctx, ctx->buffer, 1);
	if (rc < 0) {
		return rc;
	}

	op_code = ctx->buffer[0] & OPCODE_MASK;
	op_data = ctx->buffer[0] & DATA_MASK;

	switch (op_code) {
	case COPY_LEN_U4:
		state->pending = FAMILY_COPY;
		state->operation_count = op_data;
		break;
	case COPY_LEN_U12:
		rc = cpatch_patch_read(ctx, ctx->buffer, 1);
		state->pending = FAMILY_COPY;
		state->operation_count = (op_data << 8) | ctx->buffer[0];
		break;
	case COPY_LEN_U20:
		rc = cpatch_patch_read(ctx, ctx->buffer, 2);
		state->pending = FAMILY_COPY;
		state->operation_count = (op_data << 16) | sys_get_le16(ctx->buffer);
		break;
	case COPY_LEN_U32:
		rc = cpatch_patch_read(ctx, ctx->buffer, 4);
		state->pending = FAMILY_COPY;
		state->operation_count = sys_get_le32(ctx->buffer);
		break;
	case WRITE_LEN_U4:
		state->pending = FAMILY_WRITE;
		state->operation_count = op_data;
		break;
	case WRITE_LEN_U12:
		rc = cpatch_patch_read(ctx, ctx->buffer, 1);
		state->pending = FAMILY_WRITE;
		state->operation_count = (op_data << 8) | ctx->buffer[0];
		break;
	case WRITE_LEN_U20:
		rc = cpatch_patch_read(ctx, ctx->buffer, 2);
		state->pending = FAMILY_WRITE;
		state->operation_count = (op_data << 16) | sys_get_le16(ctx->buffer);
		break;
	case WRITE_LEN_U32:
		rc = cpatch_patch_read(ctx, ctx->buffer, 4);
		state->pending = FAMILY_WRITE;
		state->operation_count = sys_get_le32(ctx->buffer);
		break;
	case ADDR_SHIFT_S8:
		rc = cpatch_patch_read(ctx, ctx->buffer, 1);
		state->pending = FAMILY_ADDR;
		state->operation_count = ctx->input_offset + (int8_t)(ctx->buffer[0]);
		break;
	case ADDR_SHIFT_S16:
		rc = cpatch_patch_read(ctx, ctx->buffer, 2);
		state->pending = FAMILY_ADDR;
		state->operation_count = ctx->input_offset + (int16_t)sys_get_le16(ctx->buffer);
		break;
	case ADDR_SET_U32:
		rc = cpatch_patch_read(ctx, ctx->buffer, 4);
		state->pending = FAMILY_ADDR;
		state->operation_count = sys_get_le32(ctx->buffer);
		break;
	case PATCH:
		state->pending = FAMILY_PATCH;
		break;
	case PATCH_DELTA:
		state->pending = FAMILY_PATCH_DELTA;
		break;
	default:
		LOG_ERR("BAD OP: %d", op_code >> 4);
		return -EINVAL;
	}
	return rc;
}

static int opcode_run(struct patch_state *state)
{
	int rc = 0;

	switch (state->pending) {
	case FAMILY_ADDR:
		LOG_DBG("ADDR: %08X", state->operation_count);
		state->ctx->input_offset = state->operation_count;
		break;
	case FAMILY_COPY:
		LOG_DBG("COPY: %d", state->operation_count);
		if (state->operation_count == 0) {
			return -EINVAL;
		}
		rc = do_copy(state);
		break;
	case FAMILY_WRITE:
		LOG_DBG("WRITE: %d", state->operation_count);
		if (state->operation_count == 0) {
			return -EINVAL;
		}
		rc = do_write(state);
		break;
	case FAMILY_PATCH:
		rc = do_cpatch(state);
		break;
	case FAMILY_PATCH_DELTA:
		rc = do_cpatch_delta(state);
		break;
	default:
		return -EINVAL;
	}
	return rc;
}

int cpatch_shortest_path_apply(struct cpatch_ctx *ctx)
{
	struct patch_state state = {
		.ctx = ctx,
	};
	int rc;

	memcpy(state.delta_history, delta_history_initial, sizeof(state.delta_history));

	/* Loop over patch file */
	while (ctx->patch_offset < ctx->header->patch_file.length) {
		/* Fetch next opcode */
		rc = opcode_fetch(&state);
		if (rc < 0) {
			return rc;
		}
		/* Run the instruction */
		rc = opcode_run(&state);
		if (rc < 0) {
			return rc;
		}
		cpatch_progress_update(ctx);
	}
	return 0;
}
