/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/logging/log.h>

#include <infuse/cpatch/patch.h>

/* Updates on every 4kB chunk */
#define CALLBACK_CHUNK_MASK 0xFFFFF000

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
	OPCODE_MASK = 0xF0,
	DATA_MASK = 0x0F,
};

enum opcode_family {
	FAMILY_NONE = 0,
	FAMILY_COPY = 1,
	FAMILY_WRITE = 2,
	FAMILY_PATCH = 3,
	FAMILY_ADDR = 4,
};

struct patch_state {
	const struct flash_area *patch;
	enum opcode_family pending;
	uint32_t operation_count;
	uint32_t input_offset;
	uint32_t patch_offset;
	uint8_t buffer[64];
};

#define CPATCH_MAJOR_VERSION 1

static uint32_t progress_crc;

LOG_MODULE_REGISTER(binary_patch, CONFIG_CPATCH_LOG_LEVEL);

static int binary_patch_read(struct patch_state *state, uint8_t *ptr, size_t len)
{
	int rc;

	rc = flash_area_read(state->patch, sizeof(struct cpatch_header) + state->patch_offset, ptr,
			     len);

	state->patch_offset += len;
	return rc;
}

static int do_copy(const struct flash_area *input, struct stream_flash_ctx *output,
		   struct patch_state *state)
{
	int len, rc;

	while (state->operation_count) {
		len = MIN(state->operation_count, sizeof(state->buffer));
		rc = flash_area_read(input, state->input_offset, state->buffer, len);
		if (rc < 0) {
			return rc;
		}
		rc = stream_flash_buffered_write(output, state->buffer, len, false);
		if (rc < 0) {
			return rc;
		}
		/* Update pointers */
		state->input_offset += len;
		state->operation_count -= len;
	}

	return 0;
}

static int do_write(const struct flash_area *input, struct stream_flash_ctx *output,
		    struct patch_state *state)
{
	int len, rc;

	while (state->operation_count) {
		len = MIN(state->operation_count, sizeof(state->buffer));
		rc = binary_patch_read(state, state->buffer, len);
		if (rc < 0) {
			return rc;
		}
		rc = stream_flash_buffered_write(output, state->buffer, len, false);
		if (rc < 0) {
			return rc;
		}
		/* Update pointers */
		state->input_offset += len;
		state->operation_count -= len;
	}

	return 0;
}

static int do_cpatch(const struct flash_area *input, struct stream_flash_ctx *output,
		     struct patch_state *state)
{
	int rc;

	while (true) {
		uint8_t len;

		rc = binary_patch_read(state, &len, 1);
		if (len == 0) {
			break;
		}
		state->operation_count = len & 0x7F;
		LOG_DBG("PATCH_COPY: %d", state->operation_count);
		rc = do_copy(input, output, state);
		if (rc < 0) {
			return rc;
		}
		if (len & 0x80) {
			state->operation_count = 1;
		} else {
			rc = binary_patch_read(state, &len, 1);
			if (len == 0) {
				break;
			}
			state->operation_count = len;
		}
		LOG_DBG("PATCH_WRITE: %d", state->operation_count);
		rc = do_write(input, output, state);
		if (rc < 0) {
			return rc;
		}
	}

	return 0;
}

static int crc_update(uint8_t *buf, size_t len, size_t offset)
{
	progress_crc = crc32_ieee_update(progress_crc, buf, len);
	return 0;
}

static int opcode_fetch(struct patch_state *state)
{
	uint8_t op_code, op_data;
	int rc;

	rc = binary_patch_read(state, state->buffer, 1);
	if (rc < 0) {
		return rc;
	}

	op_code = state->buffer[0] & OPCODE_MASK;
	op_data = state->buffer[0] & DATA_MASK;

	switch (op_code) {
	case COPY_LEN_U4:
		state->pending = FAMILY_COPY;
		state->operation_count = op_data;
		break;
	case COPY_LEN_U12:
		rc = binary_patch_read(state, state->buffer, 1);
		state->pending = FAMILY_COPY;
		state->operation_count = (op_data << 8) | state->buffer[0];
		break;
	case COPY_LEN_U20:
		rc = binary_patch_read(state, state->buffer, 2);
		state->pending = FAMILY_COPY;
		state->operation_count = (op_data << 16) | sys_get_le16(state->buffer);
		break;
	case COPY_LEN_U32:
		rc = binary_patch_read(state, state->buffer, 4);
		state->pending = FAMILY_COPY;
		state->operation_count = sys_get_le32(state->buffer);
		break;
	case WRITE_LEN_U4:
		state->pending = FAMILY_WRITE;
		state->operation_count = op_data;
		break;
	case WRITE_LEN_U12:
		rc = binary_patch_read(state, state->buffer, 1);
		state->pending = FAMILY_WRITE;
		state->operation_count = (op_data << 8) | state->buffer[0];
		break;
	case WRITE_LEN_U20:
		rc = binary_patch_read(state, state->buffer, 2);
		state->pending = FAMILY_WRITE;
		state->operation_count = (op_data << 16) | sys_get_le16(state->buffer);
		break;
	case WRITE_LEN_U32:
		rc = binary_patch_read(state, state->buffer, 4);
		state->pending = FAMILY_WRITE;
		state->operation_count = sys_get_le32(state->buffer);
		break;
	case ADDR_SHIFT_S8:
		rc = binary_patch_read(state, state->buffer, 1);
		state->pending = FAMILY_ADDR;
		state->operation_count = state->input_offset + (int8_t)(state->buffer[0]);
		break;
	case ADDR_SHIFT_S16:
		rc = binary_patch_read(state, state->buffer, 2);
		state->pending = FAMILY_ADDR;
		state->operation_count = state->input_offset + (int16_t)sys_get_le16(state->buffer);
		break;
	case ADDR_SET_U32:
		rc = binary_patch_read(state, state->buffer, 4);
		state->pending = FAMILY_ADDR;
		state->operation_count = sys_get_le32(state->buffer);
		break;
	case PATCH:
		state->pending = FAMILY_PATCH;
		break;
	default:
		LOG_ERR("BAD OP: %d", op_code >> 4);
		return -EINVAL;
	}
	return rc;
}

static int opcode_run(const struct flash_area *input, struct stream_flash_ctx *output,
		      struct patch_state *state)
{
	int rc = 0;

	switch (state->pending) {
	case FAMILY_ADDR:
		LOG_DBG("ADDR: %08X", state->operation_count);
		state->input_offset = state->operation_count;
		break;
	case FAMILY_COPY:
		LOG_DBG("COPY: %d", state->operation_count);
		if (state->operation_count == 0) {
			return -EINVAL;
		}
		rc = do_copy(input, output, state);
		break;
	case FAMILY_WRITE:
		LOG_DBG("WRITE: %d", state->operation_count);
		if (state->operation_count == 0) {
			return -EINVAL;
		}
		rc = do_write(input, output, state);
		break;
	case FAMILY_PATCH:
		rc = do_cpatch(input, output, state);
		break;
	default:
		return -EINVAL;
	}
	return rc;
}

int cpatch_patch_start(const struct flash_area *input, const struct flash_area *patch,
		       struct cpatch_header *header)
{
	uint8_t buffer[64];
	uint32_t crc;
	int rc;

	/* Read header to start with */
	rc = flash_area_read(patch, 0, header, sizeof(*header));
	if (rc < 0) {
		return rc;
	}

	/* Validate header */
	if (header->magic_value != CPATCH_MAGIC_NUMBER) {
		LOG_WRN("Header magic number failure (%08X != %08X)", header->magic_value,
			CPATCH_MAGIC_NUMBER);
		return -EINVAL;
	}
	if (header->version_major != CPATCH_MAJOR_VERSION) {
		LOG_WRN("Header major version failure (%d != %d)", header->version_major,
			CPATCH_MAJOR_VERSION);
		return -EINVAL;
	}
	crc = crc32_ieee((void *)header, sizeof(*header) - sizeof(uint32_t));
	if (crc != header->header_crc) {
		LOG_WRN("Header CRC failure (%08X != %08X)", crc, header->header_crc);
		return -EINVAL;
	}

	/* Validate input file */
	rc = flash_area_crc32(input, 0, header->input_file.length, &crc, buffer, sizeof(buffer));
	if (rc < 0) {
		return rc;
	}
	if (crc != header->input_file.crc) {
		LOG_WRN("Input CRC (%08X != %08X)", crc, header->input_file.crc);
		return -EINVAL;
	}

	/* Validate patch data */
	rc = flash_area_crc32(patch, sizeof(struct cpatch_header), header->patch_file.length, &crc,
			      buffer, sizeof(buffer));
	if (rc < 0) {
		return rc;
	}
	if (crc != header->patch_file.crc) {
		LOG_WRN("Patch CRC (%08X != %08X)", crc, header->patch_file.crc);
		return -EINVAL;
	}
	return 0;
}

int cpatch_patch_apply(const struct flash_area *input, const struct flash_area *patch,
		       struct stream_flash_ctx *output, struct cpatch_header *header,
		       cpatch_progress_cb_t progress_cb)
{
	struct patch_state state = {0};
	uint32_t this_callback, last_callback = 0;
	int rc;

	output->callback = crc_update;
	progress_crc = 0x00;

	/* Loop over patch file */
	state.patch = patch;
	while (state.patch_offset < header->patch_file.length) {
		/* Fetch next opcode */
		rc = opcode_fetch(&state);
		if (rc < 0) {
			return rc;
		}
		/* Run the instruction */
		rc = opcode_run(input, output, &state);
		if (rc < 0) {
			return rc;
		}
		/* Run progress callback if we've passed a chunk boundary */
		this_callback = stream_flash_bytes_written(output) & CALLBACK_CHUNK_MASK;
		if (progress_cb && (this_callback != last_callback)) {
			last_callback = this_callback;
			progress_cb(last_callback);
		}
	}

	/* Flush any pending writes */
	rc = stream_flash_buffered_write(output, NULL, 0, true);
	if (rc < 0) {
		return rc;
	}

	/* Validate output */
	if (stream_flash_bytes_written(output) != header->output_file.length ||
	    (progress_crc != header->output_file.crc)) {
		LOG_WRN("Output failure (%d != %d) || (%08X != %08X)",
			stream_flash_bytes_written(output), header->output_file.length,
			progress_crc, header->output_file.crc);
		return -EINVAL;
	}

	return 0;
}
