/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/sys/crc.h>
#include <zephyr/logging/log.h>

#include <infuse/cpatch/patch.h>

#include "patch_internal.h"

/* Updates on every 4kB chunk */
#define CALLBACK_CHUNK_MASK 0xFFFFF000

static uint32_t progress_crc;

BUILD_ASSERT(IS_ENABLED(CONFIG_INFUSE_CPATCH_VERSION_1) ||
		     IS_ENABLED(CONFIG_INFUSE_CPATCH_VERSION_2) ||
		     IS_ENABLED(CONFIG_INFUSE_CPATCH_VERSION_3),
	     "CONFIG_INFUSE_CPATCH enabled with no supported patch appliers");

LOG_MODULE_REGISTER(binary_patch, CONFIG_CPATCH_LOG_LEVEL);

#ifdef CONFIG_INFUSE_CPATCH_READ_AHEAD
/* Read from a file of `limit` bytes, through its read-ahead cache */
static int cached_read(const struct flash_area *fa, struct cpatch_read_cache *cache, uint32_t limit,
		       uint32_t offset, uint8_t *ptr, size_t len)
{
	uint32_t chunk;
	int rc;

	while (len) {
		if (offset >= cache->offset && offset < cache->offset + cache->len) {
			chunk = MIN(len, cache->offset + cache->len - offset);
			memcpy(ptr, &cache->data[offset - cache->offset], chunk);
			offset += chunk;
			ptr += chunk;
			len -= chunk;
			continue;
		}
		if (len > sizeof(cache->data)) {
			/* Larger than the cache, read directly */
			return flash_area_read(fa, offset, ptr, len);
		}
		/* Refill from the requested offset, without reading past the end of the file */
		chunk = MAX(len, MIN(sizeof(cache->data), limit > offset ? limit - offset : 0));
		rc = flash_area_read(fa, offset, cache->data, chunk);
		if (rc < 0) {
			cache->len = 0;
			return rc;
		}
		cache->offset = offset;
		cache->len = chunk;
	}
	return 0;
}
#endif /* CONFIG_INFUSE_CPATCH_READ_AHEAD */

int cpatch_patch_read(struct cpatch_ctx *ctx, uint8_t *ptr, size_t len)
{
	uint32_t offset = sizeof(struct cpatch_header) + ctx->patch_offset;

	ctx->patch_offset += len;
#ifdef CONFIG_INFUSE_CPATCH_READ_AHEAD
	return cached_read(ctx->patch, &ctx->patch_cache,
			   sizeof(struct cpatch_header) + ctx->header->patch_file.length, offset,
			   ptr, len);
#else
	return flash_area_read(ctx->patch, offset, ptr, len);
#endif /* CONFIG_INFUSE_CPATCH_READ_AHEAD */
}

int cpatch_input_read(struct cpatch_ctx *ctx, uint32_t offset, uint8_t *ptr, size_t len)
{
#ifdef CONFIG_INFUSE_CPATCH_READ_AHEAD
	return cached_read(ctx->input, &ctx->input_cache, ctx->header->input_file.length, offset,
			   ptr, len);
#else
	return flash_area_read(ctx->input, offset, ptr, len);
#endif /* CONFIG_INFUSE_CPATCH_READ_AHEAD */
}

int cpatch_input_copy(struct cpatch_ctx *ctx, uint32_t len)
{
	uint32_t chunk;
	int rc;

	while (len) {
		chunk = MIN(len, sizeof(ctx->buffer));
		rc = cpatch_input_read(ctx, ctx->input_offset, ctx->buffer, chunk);
		if (rc < 0) {
			return rc;
		}
		rc = stream_flash_buffered_write(ctx->output, ctx->buffer, chunk, false);
		if (rc < 0) {
			return rc;
		}
		/* Update pointers */
		ctx->input_offset += chunk;
		len -= chunk;
	}

	return 0;
}

int cpatch_patch_write(struct cpatch_ctx *ctx, uint32_t len)
{
	uint32_t chunk;
	int rc;

	while (len) {
		chunk = MIN(len, sizeof(ctx->buffer));
		rc = cpatch_patch_read(ctx, ctx->buffer, chunk);
		if (rc < 0) {
			return rc;
		}
		rc = stream_flash_buffered_write(ctx->output, ctx->buffer, chunk, false);
		if (rc < 0) {
			return rc;
		}
		/* Update pointers */
		ctx->input_offset += chunk;
		len -= chunk;
	}

	return 0;
}

void cpatch_progress_update(struct cpatch_ctx *ctx)
{
	uint32_t this_callback;

	/* Run progress callback if we've passed a chunk boundary */
	this_callback = stream_flash_bytes_written(ctx->output) & CALLBACK_CHUNK_MASK;
	if (ctx->progress_cb && (this_callback != ctx->last_callback)) {
		ctx->last_callback = this_callback;
		ctx->progress_cb(ctx->last_callback, ctx->header->output_file.length);
	}
}

static cpatch_instruction_set_t instruction_set_get(uint8_t version_major)
{
	switch (version_major) {
#ifdef CONFIG_INFUSE_CPATCH_ENCODER_SHORTEST_PATH
	case CPATCH_MAJOR_VERSION:
	case CPATCH_MAJOR_VERSION_DELTA:
		return cpatch_shortest_path_apply;
#endif /* CONFIG_INFUSE_CPATCH_ENCODER_SHORTEST_PATH */
#ifdef CONFIG_INFUSE_CPATCH_ENCODER_STREAM_THUMB2
	case CPATCH_MAJOR_VERSION_STREAM_THUMB2:
		return cpatch_stream_thumb2_apply;
#endif /* CONFIG_INFUSE_CPATCH_ENCODER_STREAM_THUMB2 */
	default:
		return NULL;
	}
}

static int crc_update(uint8_t *buf, size_t len, size_t offset)
{
	progress_crc = crc32_ieee_update(progress_crc, buf, len);
	return 0;
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
	if (instruction_set_get(header->version_major) == NULL) {
		LOG_WRN("Header major version failure (%d)", header->version_major);
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
		       infuse_progress_cb_t progress_cb)
{
	cpatch_instruction_set_t instruction_set = instruction_set_get(header->version_major);
	struct cpatch_ctx ctx = {
		.input = input,
		.patch = patch,
		.output = output,
		.header = header,
		.progress_cb = progress_cb,
	};
	int rc;

	if (instruction_set == NULL) {
		return -EINVAL;
	}

	output->callback = crc_update;
	progress_crc = 0x00;

	/* Apply the patch instructions */
	rc = instruction_set(&ctx);
	if (rc < 0) {
		return rc;
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
