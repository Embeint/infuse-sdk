/**
 * @file
 * @brief CPatch element stream instruction set for Thumb-2 images (version 3)
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 * Patch data is a relocation preamble followed by a stream of elements. Each element copies
 * bytes from the input file, then performs a single operation. Every copy relocates the sites it
 * fully contains: Thumb-2 BL / B.W instructions are re-targeted by the shift of the input offset
 * they reference, and 32 bit words that point into the input image (or another region such as
 * RAM) are moved by the shift of the address they reference.
 *
 * The relocation maps are read from the patch file as required. A sparse index of map entry
 * offsets and a cache of recently used entries avoid most flash reads, both statically sized.
 */

#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

#include "patch_internal.h"

/* Element operations */
#define HISTORY_SIZE  10
#define ADD_EXPLICIT  10
#define LITERAL_1     11
#define LITERAL       12
#define LITERAL_SHIFT 13
#define SHIFT         14
#define NONE          15

/* Preamble relocation flags */
#define FLAG_BRANCH  BIT(0)
#define FLAG_POINTER BIT(1)

/* Map entries are (u24 offset, i24 shift) */
#define MAP_ENTRY_LEN 6
/* Map index of the input image, regions follow */
#define MAP_IMAGE     0
#define MAP_COUNT     (1 + CONFIG_INFUSE_CPATCH_STREAM_THUMB2_REGIONS_MAX)

/* Bytes of output produced per relocating copy chunk */
#define COPY_CHUNK  56
/* Sites are detected from the 4 bytes before and after the chunk */
#define WINDOW_SIZE (COPY_CHUNK + 8)

BUILD_ASSERT(CONFIG_INFUSE_CPATCH_STREAM_THUMB2_MAP_INDEX_SIZE > MAP_COUNT,
	     "Map index must have more entries than the number of maps");

struct reloc_map {
	/* Offset of the first map entry in the patch data */
	uint32_t patch_offset;
	/* Address of offset 0 of the map */
	uint32_t base;
	/* Number of bytes the map covers */
	uint32_t length;
	/* Number of map entries */
	uint16_t count;
	/* First slot of the map in the sparse index */
	uint16_t index_start;
};

/* Range of offsets in a map with the same shift */
struct map_cache_entry {
	uint32_t start;
	uint32_t end;
	int32_t shift: 24;
	uint32_t map: 8;
};

struct stream_state {
	struct cpatch_ctx *ctx;
	uint32_t input_length;
	uint32_t output_offset;
	uint8_t flags;
	uint8_t num_maps;
	uint8_t cache_used;
	/* Number of map entries covered by each index slot */
	uint32_t index_stride;
	uint16_t history[HISTORY_SIZE];
	struct reloc_map maps[MAP_COUNT];
	/* Offset of every `index_stride` entry of each map */
	uint32_t index[CONFIG_INFUSE_CPATCH_STREAM_THUMB2_MAP_INDEX_SIZE];
	/* Most recently used first */
	struct map_cache_entry cache[CONFIG_INFUSE_CPATCH_STREAM_THUMB2_MAP_CACHE_SIZE];
	/* Input file contents, used to detect relocation sites */
	uint8_t window[WINDOW_SIZE];
	/* Input offset of window[0] */
	int64_t window_base;
	/* Relocated output of a copy chunk */
	uint8_t output[WINDOW_SIZE];
	/* Map entries read on cache misses */
	uint8_t entries[10 * MAP_ENTRY_LEN];
};

static const uint16_t history_initial[HISTORY_SIZE] = {
	1, 0xFFFF, 2, 0xFFFE, 3, 0xFFFD, 4, 0xFFFC, 5, 0xFFFB,
};

/* Statically allocated, patch application is not reentrant */
static struct stream_state state;

LOG_MODULE_DECLARE(binary_patch, CONFIG_CPATCH_LOG_LEVEL);

static int read_u8(uint8_t *value)
{
	return cpatch_patch_read(state.ctx, value, 1);
}

static int read_uvarint(uint32_t *value)
{
	uint8_t byte;
	int rc;

	*value = 0;
	for (int shift = 0; shift < 35; shift += 7) {
		rc = read_u8(&byte);
		if (rc < 0) {
			return rc;
		}
		*value |= (uint32_t)(byte & 0x7F) << shift;
		if ((byte & 0x80) == 0) {
			return 0;
		}
	}
	return -EINVAL;
}

static int read_svarint(int32_t *value)
{
	uint32_t zigzag;
	int rc;

	rc = read_uvarint(&zigzag);
	*value = (int32_t)(zigzag >> 1) ^ -(int32_t)(zigzag & 1);
	return rc;
}

/* Read from an absolute offset in the patch data */
static int patch_read_at(uint32_t offset, void *ptr, size_t len)
{
	return flash_area_read(state.ctx->patch, sizeof(struct cpatch_header) + offset, ptr, len);
}

static int map_entry_start(const struct reloc_map *map, uint32_t entry, uint32_t *start)
{
	uint8_t raw[3];
	int rc;

	rc = patch_read_at(map->patch_offset + MAP_ENTRY_LEN * entry, raw, sizeof(raw));
	if (rc < 0) {
		return rc;
	}
	*start = sys_get_le24(raw);
	return 0;
}

static int preamble_load(void)
{
	struct cpatch_ctx *ctx = state.ctx;
	struct reloc_map *map;
	uint32_t total = 0;
	uint8_t raw[10];
	uint32_t slots;
	uint8_t regions;
	uint16_t slot;
	int rc;

	rc = read_u8(&state.flags);
	if (rc < 0) {
		return rc;
	}
	if (state.flags & ~(FLAG_BRANCH | FLAG_POINTER)) {
		LOG_WRN("Unsupported relocation flags (%02X)", state.flags);
		return -EINVAL;
	}
	state.num_maps = 0;
	if (state.flags == 0) {
		return 0;
	}

	/* Input image map */
	rc = cpatch_patch_read(ctx, raw, 2);
	if (rc < 0) {
		return rc;
	}
	map = &state.maps[MAP_IMAGE];
	*map = (struct reloc_map){
		.patch_offset = ctx->patch_offset,
		.length = state.input_length,
		.count = sys_get_le16(raw),
	};
	ctx->patch_offset += MAP_ENTRY_LEN * map->count;
	state.num_maps = 1;

	if (state.flags & FLAG_POINTER) {
		rc = cpatch_patch_read(ctx, raw, 5);
		if (rc < 0) {
			return rc;
		}
		map->base = sys_get_le32(raw);
		regions = raw[4];
		if (regions > CONFIG_INFUSE_CPATCH_STREAM_THUMB2_REGIONS_MAX) {
			LOG_WRN("Too many pointer regions (%d > %d)", regions,
				CONFIG_INFUSE_CPATCH_STREAM_THUMB2_REGIONS_MAX);
			return -EINVAL;
		}
		for (int i = 0; i < regions; i++) {
			rc = cpatch_patch_read(ctx, raw, 10);
			if (rc < 0) {
				return rc;
			}
			map = &state.maps[state.num_maps++];
			*map = (struct reloc_map){
				.patch_offset = ctx->patch_offset,
				.base = sys_get_le32(raw),
				.length = sys_get_le32(raw + 4),
				.count = sys_get_le16(raw + 8),
			};
			ctx->patch_offset += MAP_ENTRY_LEN * map->count;
		}
	}
	if (ctx->patch_offset > ctx->header->patch_file.length) {
		return -EINVAL;
	}

	/* Index stride such that the slots of every map fit in the index */
	for (int i = 0; i < state.num_maps; i++) {
		total += state.maps[i].count;
	}
	slots = CONFIG_INFUSE_CPATCH_STREAM_THUMB2_MAP_INDEX_SIZE - state.num_maps;
	state.index_stride = MAX(1, DIV_ROUND_UP(total, slots));
	slot = 0;
	for (int i = 0; i < state.num_maps; i++) {
		map = &state.maps[i];
		map->index_start = slot;
		for (uint32_t entry = 0; entry < map->count; entry += state.index_stride) {
			rc = map_entry_start(map, entry, &state.index[slot++]);
			if (rc < 0) {
				return rc;
			}
		}
	}
	LOG_DBG("%d maps, %d entries, index stride %d", state.num_maps, total, state.index_stride);
	return 0;
}

static void cache_insert(struct map_cache_entry entry)
{
	if (state.cache_used < ARRAY_SIZE(state.cache)) {
		state.cache_used++;
	}
	memmove(&state.cache[1], &state.cache[0], (state.cache_used - 1) * sizeof(state.cache[0]));
	state.cache[0] = entry;
}

/* Find the map entry containing `offset` from the patch file */
static int map_search(uint8_t map_id, uint32_t offset, struct map_cache_entry *found)
{
	const struct reloc_map *map = &state.maps[map_id];
	const uint32_t *index = &state.index[map->index_start];
	uint32_t slots = DIV_ROUND_UP(map->count, state.index_stride);
	uint32_t lo = 0, hi = slots, entry, block_end, chunk;
	int rc;

	found->map = map_id;
	if (map->count == 0 || offset < index[0]) {
		/* Offsets before the first entry do not move */
		found->start = 0;
		found->end = map->count ? index[0] : map->length;
		found->shift = 0;
		return 0;
	}

	/* Last index slot starting at or before the offset */
	while (hi - lo > 1) {
		uint32_t mid = (lo + hi) / 2;

		if (index[mid] <= offset) {
			lo = mid;
		} else {
			hi = mid;
		}
	}
	entry = lo * state.index_stride;
	block_end = MIN(entry + state.index_stride, map->count);
	found->end = (lo + 1 < slots) ? index[lo + 1] : map->length;

	/* Last entry in the block starting at or before the offset */
	while (entry < block_end) {
		chunk = MIN(block_end - entry, sizeof(state.entries) / MAP_ENTRY_LEN);
		rc = patch_read_at(map->patch_offset + MAP_ENTRY_LEN * entry, state.entries,
				   MAP_ENTRY_LEN * chunk);
		if (rc < 0) {
			return rc;
		}
		for (uint32_t i = 0; i < chunk; i++) {
			const uint8_t *e = &state.entries[MAP_ENTRY_LEN * i];
			uint32_t start = sys_get_le24(e);

			if (start > offset) {
				found->end = start;
				return 0;
			}
			found->start = start;
			/* Sign extend from 24 bits */
			found->shift = (int32_t)(sys_get_le24(e + 3) << 8) >> 8;
		}
		entry += chunk;
	}
	return 0;
}

/* Shift of an offset into a map */
static int map_shift(uint8_t map_id, uint32_t offset, int32_t *shift)
{
	struct map_cache_entry entry;
	int rc;

	if (offset >= state.maps[map_id].length) {
		*shift = 0;
		return 0;
	}
	for (int i = 0; i < state.cache_used; i++) {
		entry = state.cache[i];
		if (entry.map == map_id && entry.start <= offset && offset < entry.end) {
			/* Move to the front */
			memmove(&state.cache[1], &state.cache[0], i * sizeof(state.cache[0]));
			state.cache[0] = entry;
			*shift = entry.shift;
			return 0;
		}
	}
	rc = map_search(map_id, offset, &entry);
	if (rc < 0) {
		return rc;
	}
	cache_insert(entry);
	*shift = entry.shift;
	return 0;
}

static uint16_t window_u16(int64_t address)
{
	return sys_get_le16(&state.window[address - state.window_base]);
}

/* Offset of a Thumb-2 BL or B.W (encoding T4) starting at `address` */
static bool branch_pattern(int64_t address, int32_t *offset)
{
	uint16_t h1, h2;
	uint32_t s, i1, i2, value;

	if (address < 0 || (address & 1) || address + 4 > state.input_length) {
		return false;
	}
	h1 = window_u16(address);
	h2 = window_u16(address + 2);
	if ((h1 & 0xF800) != 0xF000 || ((h2 & 0xD000) != 0xD000 && (h2 & 0xD000) != 0x9000)) {
		return false;
	}
	s = (h1 >> 10) & 1;
	i1 = 1 - (((h2 >> 13) & 1) ^ s);
	i2 = 1 - (((h2 >> 11) & 1) ^ s);
	value = (s << 24) | (i1 << 23) | (i2 << 22) | ((h1 & 0x3FF) << 12) | ((h2 & 0x7FF) << 1);
	/* Sign extend from 25 bits */
	*offset = (int32_t)(value << 7) >> 7;
	return true;
}

/* A branch pattern is a site unless the previous halfword also starts a branch pattern */
static bool branch_site(int64_t address, int32_t *offset)
{
	int32_t unused;

	return (state.flags & FLAG_BRANCH) && branch_pattern(address, offset) &&
	       !branch_pattern(address - 2, &unused);
}

static bool branch_encode(uint16_t h2_original, int64_t offset, uint8_t out[4])
{
	uint32_t value, shifted, jump1, jump2;

	if (offset < -(1 << 24) || offset >= (1 << 24) || (offset & 1)) {
		return false;
	}
	value = (uint32_t)offset & 0x1FFFFFF;
	shifted = value >> 24;
	jump1 = (1 - ((value >> 23) & 1)) ^ shifted;
	jump2 = (1 - ((value >> 22) & 1)) ^ shifted;
	sys_put_le16(0xF000 | (shifted << 10) | ((value >> 12) & 0x3FF), out);
	sys_put_le16((h2_original & 0xD000) | (jump1 << 13) | (jump2 << 11) |
			     ((value >> 1) & 0x7FF),
		     out + 2);
	return true;
}

/* Map and offset that the word at `address` points to, if it is relocated as a pointer */
static bool pointer_target(int64_t address, uint8_t *map_id, uint32_t *offset)
{
	int32_t unused;
	uint32_t value;
	bool found = false;

	if (!(state.flags & FLAG_POINTER) || (address & 3) || address + 4 > state.input_length) {
		return false;
	}
	value = sys_get_le32(&state.window[address - state.window_base]);
	for (int i = 0; i < state.num_maps; i++) {
		*offset = value - state.maps[i].base;
		if (*offset < state.maps[i].length) {
			*map_id = i;
			found = true;
			break;
		}
	}
	/* Words that overlap a branch site are not pointers */
	return found && !branch_site(address - 2, &unused) && !branch_site(address, &unused) &&
	       !branch_site(address + 2, &unused);
}

/* Relocate the site at input `address` into `out`, output at `output_address` */
static int site_relocate(int64_t address, uint32_t output_address, uint8_t out[4], bool *site)
{
	uint32_t offset;
	int32_t branch, shift;
	int64_t target;
	uint8_t map_id;
	int rc;

	*site = false;
	if (branch_site(address, &branch)) {
		*site = true;
		target = address + 4 + branch;
		if (target < 0 || target >= state.input_length) {
			return 0;
		}
		rc = map_shift(MAP_IMAGE, target, &shift);
		if (rc < 0) {
			return rc;
		}
		(void)branch_encode(window_u16(address + 2), target + shift - (output_address + 4),
				    out);
		return 0;
	}
	if (pointer_target(address, &map_id, &offset)) {
		*site = true;
		rc = map_shift(map_id, offset, &shift);
		if (rc < 0) {
			return rc;
		}
		sys_put_le32(sys_get_le32(out) + shift, out);
	}
	return 0;
}

/* Copy from the input with relocation, writing output linearly */
static int stream_copy(uint32_t length)
{
	struct cpatch_ctx *ctx = state.ctx;
	uint32_t pos = ctx->input_offset;
	uint32_t end, chunk_end, win_end;
	int64_t win_start;
	bool site;
	int rc;

	if (pos > state.input_length || length > state.input_length - pos) {
		LOG_WRN("Copy outside input (%u + %u)", pos, length);
		return -EINVAL;
	}
	if (state.flags == 0) {
		state.output_offset += length;
		return cpatch_input_copy(ctx, length);
	}

	end = pos + length;
	while (pos < end) {
		/* Halfword aligned, so that sites extend at most 2 bytes past the chunk */
		chunk_end = MIN(end, ROUND_DOWN(pos + COPY_CHUNK, 2));
		/* Sites are detected from up to 4 bytes either side of their start */
		win_start = MAX(0, (int64_t)pos - 4);
		win_end = MIN(state.input_length, chunk_end + 4);
		rc = cpatch_input_read(ctx, win_start, state.window, win_end - win_start);
		if (rc < 0) {
			return rc;
		}
		state.window_base = win_start;
		memcpy(state.output, &state.window[pos - win_start], MIN(end, chunk_end + 2) - pos);

		for (uint32_t a = ROUND_UP(pos, 2); a < chunk_end && a + 4 <= end; a += 2) {
			uint8_t *out = &state.output[a - pos];

			rc = site_relocate(a, state.output_offset + (a - pos), out, &site);
			if (rc < 0) {
				return rc;
			}
			if (site) {
				/* Sites never overlap, extend the chunk to the end of the site */
				chunk_end = MAX(chunk_end, a + 4);
				a += 2;
			}
		}
		rc = stream_flash_buffered_write(ctx->output, state.output, chunk_end - pos, false);
		if (rc < 0) {
			return rc;
		}
		state.output_offset += chunk_end - pos;
		pos = chunk_end;
	}
	ctx->input_offset = end;
	return 0;
}

static uint16_t history_use(uint8_t index, uint16_t delta)
{
	/* Explicit deltas evict the least recently used entry */
	if (index == ADD_EXPLICIT) {
		index = HISTORY_SIZE - 1;
	} else {
		delta = state.history[index];
	}
	memmove(&state.history[1], &state.history[0], index * sizeof(uint16_t));
	state.history[0] = delta;
	return delta;
}

static int stream_add(uint16_t delta)
{
	struct cpatch_ctx *ctx = state.ctx;
	uint8_t value[2];
	int rc;

	if (ctx->input_offset > state.input_length || state.input_length - ctx->input_offset < 2) {
		return -EINVAL;
	}
	rc = cpatch_input_read(ctx, ctx->input_offset, value, sizeof(value));
	if (rc < 0) {
		return rc;
	}
	sys_put_le16(sys_get_le16(value) + delta, value);
	rc = stream_flash_buffered_write(ctx->output, value, sizeof(value), false);
	ctx->input_offset += 2;
	state.output_offset += 2;
	return rc;
}

static int stream_literal(uint32_t length)
{
	state.output_offset += length;
	return cpatch_patch_write(state.ctx, length);
}

static int element_run(void)
{
	struct cpatch_ctx *ctx = state.ctx;
	uint32_t copy, length = 0;
	uint8_t header, op;
	uint16_t delta = 0;
	int32_t shift = 0;
	int rc;

	rc = read_u8(&header);
	if (rc < 0) {
		return rc;
	}
	copy = header >> 4;
	op = header & 0x0F;
	if (copy == 15) {
		rc = read_uvarint(&length);
		if (rc < 0) {
			return rc;
		}
		copy += length;
	}
	/* Parse the operation before copying, the operation data follows the header */
	switch (op) {
	case ADD_EXPLICIT:
		rc = cpatch_patch_read(ctx, ctx->buffer, 2);
		delta = sys_get_le16(ctx->buffer);
		break;
	case LITERAL:
		rc = read_uvarint(&length);
		length += 2;
		break;
	case LITERAL_SHIFT:
		rc = read_uvarint(&length);
		length += 1;
		break;
	case SHIFT:
		rc = read_svarint(&shift);
		break;
	case NONE:
		if (copy == 0) {
			LOG_WRN("Unsupported extended operation");
			return -EINVAL;
		}
		break;
	default:
		break;
	}
	if (rc < 0) {
		return rc;
	}

	LOG_DBG("COPY: %d OP: %d", copy, op);
	if (copy) {
		rc = stream_copy(copy);
		if (rc < 0) {
			return rc;
		}
	}
	switch (op) {
	case LITERAL_1:
		return stream_literal(1);
	case LITERAL:
		return stream_literal(length);
	case LITERAL_SHIFT:
		/* Shift follows the literal data */
		rc = stream_literal(length);
		if (rc < 0) {
			return rc;
		}
		rc = read_svarint(&shift);
		ctx->input_offset += shift;
		return rc;
	case SHIFT:
		ctx->input_offset += shift;
		return 0;
	case NONE:
		return 0;
	default:
		return stream_add(history_use(op, delta));
	}
}

int cpatch_stream_thumb2_apply(struct cpatch_ctx *ctx)
{
	int rc;

	state = (struct stream_state){
		.ctx = ctx,
		.input_length = ctx->header->input_file.length,
	};
	memcpy(state.history, history_initial, sizeof(state.history));

	rc = preamble_load();
	if (rc < 0) {
		return rc;
	}
	while (ctx->patch_offset < ctx->header->patch_file.length) {
		rc = element_run();
		if (rc < 0) {
			return rc;
		}
		cpatch_progress_update(ctx);
	}
	return 0;
}
