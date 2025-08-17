/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#define DT_DRV_COMPAT embeint_data_logger

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/net_buf.h>
#include <zephyr/pm/device_runtime.h>

#include <infuse/data_logger/logger.h>
#include <infuse/drivers/watchdog.h>

#include "backends/common.h"

#define IS_PERSISTENT_LOGGER(api) (api->read != NULL)
#define BLOCK_QUEUE_MAX_SIZE      512

/* Block header on the ram buffer */
struct ram_buf_header {
	uint8_t block_type;
	uint16_t block_len;
} __packed;

#ifdef CONFIG_DATA_LOGGER_OFFLOAD_WRITES

struct net_buf_ctx {
	const struct device *dev;
	enum infuse_type type;
	bool flush;
};

NET_BUF_POOL_DEFINE(block_queue_pool, CONFIG_DATA_LOGGER_OFFLOAD_MAX_PENDING, BLOCK_QUEUE_MAX_SIZE,
		    sizeof(struct net_buf_ctx), NULL);
K_FIFO_DEFINE(block_commit_fifo);

#endif /* CONFIG_DATA_LOGGER_OFFLOAD_WRITES */

LOG_MODULE_REGISTER(data_logger, CONFIG_DATA_LOGGER_LOG_LEVEL);

void data_logger_get_state(const struct device *dev, struct data_logger_state *state)
{
	const struct data_logger_common_config *cfg = dev->config;
	struct data_logger_common_data *data = dev->data;
	const struct data_logger_api *api = dev->api;

	state->bytes_logged = data->bytes_logged;
	state->logical_blocks = data->logical_blocks;
	state->physical_blocks = data->physical_blocks;
	state->boot_block = data->boot_block;
	state->current_block = data->current_block;
	state->earliest_block = data->earliest_block;
	state->block_size = data->block_size;
	state->block_overhead =
		api->read == NULL ? 0 : sizeof(struct data_logger_persistent_block_header);
	state->erase_unit = data->erase_size;
	state->requires_full_block_write = cfg->requires_full_block_write;
}

static void handle_block_write_fail(const struct device *dev, enum infuse_type type, void *block,
				    uint16_t block_len, int reason)
{
	struct data_logger_common_data *data = dev->data;
	struct data_logger_cb *cb;

	/* Notify subscribers */
	SYS_SLIST_FOR_EACH_CONTAINER(&data->callbacks, cb, node) {
		if (cb->write_failure) {
			cb->write_failure(dev, type, block, block_len, reason, cb->user_data);
		}
	}
}

static int do_block_write(const struct device *dev, enum infuse_type type, void *block,
			  uint16_t block_len)
{
	struct data_logger_common_data *data = dev->data;
	const struct data_logger_api *api = dev->api;
	uint16_t erase_blocks = data->erase_size / data->block_size;
	uint32_t phy_block = data->current_block % data->physical_blocks;
	int rc;

	LOG_DBG("%s writing to logical block %u (Phy block %u)", dev->name, data->current_block,
		phy_block);

	/* Request backend to be powered */
	rc = pm_device_runtime_get(dev);
	if (rc < 0) {
		goto done;
	}

	/* Erase next chunk if required */
	if ((data->current_block >= data->physical_blocks) &&
	    ((data->current_block % erase_blocks) == 0)) {
		LOG_DBG("%s preparing block for write", dev->name);
		rc = api->erase(dev, phy_block, erase_blocks);
		if (rc < 0) {
			LOG_ERR("%s failed to prepare block (%d)", dev->name, rc);
			goto release;
		}
		/* Old data is no longer present */
		data->earliest_block += erase_blocks;
	}

	/* Add persistent block header if required */
	if (IS_PERSISTENT_LOGGER(api)) {
		struct data_logger_persistent_block_header *header = block;

		header->block_type = type;
		header->block_wrap = (data->current_block / data->physical_blocks) + 1;
	}

	/* Write block to backend */
	rc = api->write(dev, phy_block, type, block, block_len);
	if (rc < 0) {
		LOG_ERR("%s failed to write to backend", dev->name);
	}

release:

	/* Release device after a delay */
	(void)pm_device_runtime_put_async(dev, K_MSEC(100));

done:
	if (rc == 0) {
		data->bytes_logged += block_len;
		data->current_block += 1;
	} else {
		handle_block_write_fail(dev, type, block, block_len, rc);
	}
	return rc;
}

#ifdef CONFIG_DATA_LOGGER_RAM_BUFFER
#ifdef CONFIG_DATA_LOGGER_BURST_WRITES
static int do_ram_buffer_flush_burst(const struct device *dev)
{
	const struct data_logger_common_config *config = dev->config;
	struct data_logger_common_data *data = dev->data;
	const struct data_logger_api *api = dev->api;
	uint32_t pending = data->ram_buf_offset / data->block_size;
	int64_t flush_end, flush_start = k_uptime_get();
	int32_t flush_duration;
	int rc;

	LOG_DBG("%s writing %d blocks to logical block %u", dev->name, pending,
		data->current_block);

	/* Request backend to be powered */
	rc = pm_device_runtime_get(dev);
	if (rc < 0) {
		/* Reset pending data, not much else we can do */
		data->ram_buf_offset = 0;
		return rc;
	}

	/* Do the burst write */
	rc = api->write_burst(dev, data->current_block, pending, config->ram_buf_data);

	/* Release device after a delay */
	(void)pm_device_runtime_put_async(dev, K_MSEC(100));

	flush_end = k_uptime_get();
	data->ram_buf_offset = 0;
	flush_duration = flush_end - flush_start;
	LOG_INF("%s -> Flushed %d blocks in %d ms", dev->name, pending, flush_duration);

	if (rc != -ENOMEM) {
		data->bytes_logged += pending * data->block_size;
		data->current_block += pending;
	}
	return rc;
}

static int do_block_write_ram_buffer_burst(const struct device *dev, enum infuse_type type,
					   void *block, uint16_t block_len)
{
	const struct data_logger_common_config *config = dev->config;
	struct data_logger_common_data *data = dev->data;
	__maybe_unused const struct data_logger_api *api = dev->api;
	struct data_logger_persistent_block_header *header = block;
	uint32_t pending;

	/* We have already asserted in `init` that all blocks are the same size */
	__ASSERT_NO_MSG(IS_PERSISTENT_LOGGER(api));
	__ASSERT_NO_MSG(block_len == data->block_size);

	/* Push on the block prefix */
	header->block_type = type;
	header->block_wrap = (data->current_block / data->physical_blocks) + 1;

	/* Copy block into RAM buffer */
	memcpy(config->ram_buf_data + data->ram_buf_offset, block, block_len);
	data->ram_buf_offset += block_len;
	pending = data->ram_buf_offset / data->block_size;
	LOG_DBG("RAM buffer: %d/%d", data->ram_buf_offset, config->ram_buf_len);

	if ((data->ram_buf_offset != config->ram_buf_len) &&
	    (data->current_block + pending != data->logical_blocks)) {
		/* Space for more blocks in the buffer and not at the end of space */
		return 0;
	}

	/* Flush the RAM buffer */
	return do_ram_buffer_flush_burst(dev);
}
#endif /* CONFIG_DATA_LOGGER_BURST_WRITES */

static void do_ram_buffer_flush_single(const struct device *dev)
{
	const struct data_logger_common_config *config = dev->config;
	struct data_logger_common_data *data = dev->data;
	int64_t flush_end, flush_start = k_uptime_get();
	int32_t flush_duration;
	struct ram_buf_header *hdr;
	uint32_t offset = 0;
	uint32_t flushed = 0;
	int rc;

	while (offset < data->ram_buf_offset) {
		/* Push the next block */
		hdr = (void *)(config->ram_buf_data + offset);
		offset += sizeof(*hdr);
		rc = do_block_write(dev, hdr->block_type, config->ram_buf_data + offset,
				    hdr->block_len);
		offset += hdr->block_len;
		flushed += 1;

		LOG_DBG("Flushed %d byte %02X block (%d)", hdr->block_len, hdr->block_type, rc);
	}
	flush_end = k_uptime_get();
	data->ram_buf_offset = 0;
	flush_duration = flush_end - flush_start;
	LOG_INF("%s -> Flushed %d blocks in %d ms", dev->name, flushed, flush_duration);
}

static int do_block_write_ram_buffer_single(const struct device *dev, enum infuse_type type,
					    void *block, uint16_t block_len)
{
	const struct data_logger_common_config *config = dev->config;
	struct data_logger_common_data *data = dev->data;
	uint32_t space = config->ram_buf_len - data->ram_buf_offset;

	if (space > (sizeof(struct ram_buf_header) + block_len)) {
		/* Space for this block, add header and data to FIFO */
		struct ram_buf_header header = {
			.block_type = type,
			.block_len = block_len,
		};

		LOG_DBG("Pending %d byte %02X block", block_len, type);
		memcpy(config->ram_buf_data + data->ram_buf_offset, &header, sizeof(header));
		data->ram_buf_offset += sizeof(header);
		memcpy(config->ram_buf_data + data->ram_buf_offset, block, block_len);
		data->ram_buf_offset += block_len;
		return 0;
	} else if (data->ram_buf_offset == 0) {
		/* No space in RAM buffer, no data logged, write per usual */
	} else {
		/* No space, data previously logged. Flush all pending data */
		do_ram_buffer_flush_single(dev);
	}
	return do_block_write(dev, type, block, block_len);
}

static int do_block_write_ram_buffer(const struct device *dev, enum infuse_type type, void *block,
				     uint16_t block_len)
{
#ifdef CONFIG_DATA_LOGGER_BURST_WRITES
	const struct data_logger_api *api = dev->api;

	if (api->write_burst) {
		return do_block_write_ram_buffer_burst(dev, type, block, block_len);
	}
#endif /* CONFIG_DATA_LOGGER_BURST_WRITES */
	return do_block_write_ram_buffer_single(dev, type, block, block_len);
}

static int do_ram_buffer_flush(const struct device *dev)
{
	struct data_logger_common_data *data = dev->data;

	if (data->ram_buf_offset == 0) {
		return 0;
	}

#ifdef CONFIG_DATA_LOGGER_BURST_WRITES
	const struct data_logger_api *api = dev->api;

	if (api->write_burst) {
		return do_ram_buffer_flush_burst(dev);
	}
#endif /* CONFIG_DATA_LOGGER_BURST_WRITES */
	do_ram_buffer_flush_single(dev);
	return 0;
}

#endif /* CONFIG_DATA_LOGGER_RAM_BUFFER */

static int handle_block_write(const struct device *dev, enum infuse_type type, void *block,
			      uint16_t block_len)
{
	const struct data_logger_common_config *config = dev->config;
	struct data_logger_common_data *data = dev->data;
	uint8_t unaligned, padding;

	/* Handle write alignment */
	if (!config->requires_full_block_write && (config->block_write_align > 1)) {
		unaligned = block_len % config->block_write_align;
		if (unaligned > 0) {
			/* Pad the block to the alignment requirement */
			padding = config->block_write_align - unaligned;
			memset((uint8_t *)block + block_len, data->erase_val, padding);
			block_len += padding;
		}
	}

#ifdef CONFIG_DATA_LOGGER_RAM_BUFFER
	if (config->ram_buf_len) {
		/* Perform RAM buffering */
		return do_block_write_ram_buffer(dev, type, block, block_len);
	}
#endif /* CONFIG_DATA_LOGGER_RAM_BUFFER */

	/* Perform the block write */
	return do_block_write(dev, type, block, block_len);
}

#ifdef CONFIG_DATA_LOGGER_OFFLOAD_WRITES

INFUSE_WATCHDOG_REGISTER_SYS_INIT(rpc_dl, CONFIG_DATA_LOGGER_OFFLOAD_WATCHDOG, wdog_channel,
				  loop_period);

static int logger_commit_thread_fn(void *a, void *b, void *c)
{
	struct net_buf_ctx *ctx;
	struct net_buf *buf;
	int rc;

	infuse_watchdog_thread_register(wdog_channel, _current);
	for (;;) {
		buf = k_fifo_get(&block_commit_fifo, loop_period);
		infuse_watchdog_feed(wdog_channel);
		if (buf == NULL) {
			continue;
		}
		ctx = net_buf_user_data(buf);

#ifdef CONFIG_DATA_LOGGER_RAM_BUFFER
		if (ctx->flush) {
			const struct device *dev = ctx->dev;

			/* Free the buffer, we don't need it */
			net_buf_unref(buf);

			/* Perform the flush */
			(void)do_ram_buffer_flush(dev);

			/* Feed watchdog before sleeping again */
			infuse_watchdog_feed(wdog_channel);
			continue;
		}

#endif /* CONFIG_DATA_LOGGER_RAM_BUFFER */

		rc = handle_block_write(ctx->dev, ctx->type, buf->data, buf->len);
		if (rc < 0) {
			LOG_ERR("Offload failed to write block on %s (%d)", ctx->dev->name, rc);
		}
		net_buf_unref(buf);

		/* Feed watchdog before sleeping again */
		infuse_watchdog_feed(wdog_channel);
	}
	return 0;
}

K_THREAD_DEFINE(logger_commit_thread, CONFIG_DATA_LOGGER_OFFLOAD_STACK_SIZE,
		logger_commit_thread_fn, NULL, NULL, NULL, 5, K_ESSENTIAL, 0);

#endif /* CONFIG_DATA_LOGGER_OFFLOAD_WRITES */

int data_logger_block_write(const struct device *dev, enum infuse_type type, void *block,
			    uint16_t block_len)
{
	struct data_logger_common_data *data = dev->data;
	int rc;

	/* Validate block length */
	if (block_len > data->block_size) {
		rc = data->block_size == 0 ? -ENOTCONN : -EINVAL;
		goto error;
	}
	/* Check there is still space on the logger */
	if (data->current_block >= data->logical_blocks) {
		rc = -ENOMEM;
		goto error;
	}
	/* Check if logger is currently erasing */
	if (data->flags & DATA_LOGGER_FLAGS_ERASING) {
		return 0;
	}

	/* Logging on the system workqueue can cause deadlocks and should be avoided */
	if (k_current_get() == k_work_queue_thread_get(&k_sys_work_q)) {
		LOG_WRN("%s logging on system workqueue", dev->name);
	}

#ifdef CONFIG_DATA_LOGGER_OFFLOAD_WRITES
	const struct data_logger_common_config *config = dev->config;

	if (config->queued_writes) {
		/* Handle block write directly if backend handles write queuing itself.
		 * Note that with extra RAM buffering this function may block the calling
		 * context until most of the buffer has been sent.
		 */
		return handle_block_write(dev, type, block, block_len);
	}
	struct net_buf_ctx *ctx;
	struct net_buf *buf;

	/* Let commit thread handle block write.
	 * This resolves potential stack overflow from logging
	 * in different contexts to different backends.
	 */
	buf = net_buf_alloc(&block_queue_pool, K_FOREVER);
	ctx = net_buf_user_data(buf);
	ctx->dev = dev;
	ctx->type = type;
	ctx->flush = false;
	net_buf_add_mem(buf, block, block_len);
	k_fifo_put(&block_commit_fifo, buf);
	return 0;
#else
	return handle_block_write(dev, type, block, block_len);
#endif /* CONFIG_DATA_LOGGER_OFFLOAD_WRITES */
error:
	handle_block_write_fail(dev, type, block, block_len, rc);
	return rc;
}

int data_logger_block_read(const struct device *dev, uint32_t block_idx, uint16_t block_offset,
			   void *block, uint16_t block_len)
{
	struct data_logger_common_data *data = dev->data;
	const struct data_logger_api *api = dev->api;
	uint32_t phy_block = block_idx % data->physical_blocks;
	uint32_t end_logical =
		((data->block_size * block_idx) + block_offset + block_len - 1) / data->block_size;
	uint32_t end_phy = end_logical % data->physical_blocks;
	uint32_t second_read = 0;
	int rc;

	/* Can only read from persistent loggers */
	if (!IS_PERSISTENT_LOGGER(api)) {
		return -ENOTSUP;
	}

	/* Check if logger is currently erasing */
	if (data->flags & DATA_LOGGER_FLAGS_ERASING) {
		return -EBUSY;
	}

	/* Data that does not exist */
	if ((block_idx < data->earliest_block) || (end_logical >= data->current_block) ||
	    (block_offset >= data->block_size)) {
		return -ENOENT;
	}

	/* Read goes across the wrap boundary */
	if (end_phy < phy_block) {
		uint32_t bytes_to_wrap =
			(data->physical_blocks - phy_block) * data->block_size - block_offset;

		LOG_DBG("%s read wraps across boundary after %d bytes", dev->name, bytes_to_wrap);
		second_read = block_len - bytes_to_wrap;
		block_len -= second_read;
	}

	/* Request backend to be powered */
	rc = pm_device_runtime_get(dev);
	if (rc < 0) {
		return rc;
	}

	/* Read block from backend */
	rc = api->read(dev, phy_block, block_offset, block, block_len);
	if (rc < 0) {
		LOG_ERR("%s failed to read from backend", dev->name);
	}
	/* Read data remaining after wrap */
	if (second_read) {
		block = (uint8_t *)block + block_len;
		LOG_DBG("%s reading remaining %d bytes", dev->name, second_read);
		rc = api->read(dev, 0, 0, block, second_read);
	}

	/* Release device after a delay */
	(void)pm_device_runtime_put_async(dev, K_MSEC(100));

	return rc;
}

int data_logger_erase(const struct device *dev, bool erase_all,
		      void (*erase_progress)(uint32_t blocks_erased))
{
	struct data_logger_common_data *data = dev->data;
	const struct data_logger_api *api = dev->api;
	uint32_t block_hint;
	int rc;

	/* Can only erase persistent loggers */
	if (!IS_PERSISTENT_LOGGER(api) || (api->reset == NULL)) {
		return -ENOTSUP;
	}

	/* Request backend to be powered */
	rc = pm_device_runtime_get(dev);
	if (rc < 0) {
		LOG_ERR("Failed to power up for reset (%d)", rc);
		return rc;
	}

	if (erase_all) {
		block_hint = data->physical_blocks;
	} else {
		block_hint = MIN(data->current_block, data->physical_blocks);
	}

	/* Set erasing flag */
	data->flags |= DATA_LOGGER_FLAGS_ERASING;

	/* Erase the underlying logger */
	rc = api->reset(dev, block_hint, erase_progress);

	/* Reset block counters only on successful erase */
	if (rc == 0) {
		data->current_block = 0;
		data->earliest_block = 0;
		data->boot_block = 0;
	}

	/* Release device */
	(void)pm_device_runtime_put(dev);

	/* Clear erasing flag */
	data->flags &= ~DATA_LOGGER_FLAGS_ERASING;
	return rc;
}

int data_logger_flush(const struct device *dev)
{
	int rc = 0;

#ifdef CONFIG_DATA_LOGGER_RAM_BUFFER
	const struct data_logger_common_config *config = dev->config;

	if (config->ram_buf_data == NULL) {
		/* No RAM buffer, nothing to do */
		return 0;
	}

#ifdef CONFIG_DATA_LOGGER_OFFLOAD_WRITES
	struct net_buf_ctx *ctx;
	struct net_buf *buf;

	/* Request offload thread to perform the flush */
	buf = net_buf_alloc(&block_queue_pool, K_FOREVER);
	ctx = net_buf_user_data(buf);
	ctx->dev = dev;
	ctx->flush = true;
	k_fifo_put(&block_commit_fifo, buf);
#else
	rc = do_ram_buffer_flush(dev);
#endif /* CONFIG_DATA_LOGGER_OFFLOAD_WRITES */
#endif /* CONFIG_DATA_LOGGER_RAM_BUFFER */

	return rc;
}

static int current_block_search(const struct device *dev, uint8_t counter)
{
	struct data_logger_common_data *data = dev->data;
	const struct data_logger_api *api = dev->api;
	struct data_logger_persistent_block_header temp;
	uint32_t high, low, mid, res;
	uint32_t max_search;
	int rc = -ENOSYS;

	if (api->search_hint) {
		/* Ask driver for search range hints */
		rc = api->search_hint(dev, &low, &high);
	}
	if (rc < 0) {
		/* Search hint not supported or failed */
		high = data->physical_blocks - 1;
		low = 0;
	}
	res = low;

	/* Binary search for last block where block_wrap == counter */
	while (low <= high) {
		mid = (low + high) / 2;
		rc = api->read(dev, mid, 0, &temp, sizeof(temp));
		if (rc < 0) {
			return rc;
		}
		if (temp.block_wrap == counter) {
			low = mid + 1;
			res = mid;
		} else {
			high = mid - 1;
		}
	}
	data->current_block = ((counter - 1) * data->physical_blocks) + res + 1;

	/* Logger has not yet wrapped, all data still present */
	if (counter == 1) {
		data->earliest_block = 0;
		return 0;
	}

	/* Find next block with valid data */
	__ASSERT_NO_MSG(data->current_block >= data->physical_blocks);
	data->earliest_block = data->current_block - data->physical_blocks;
	res = (data->earliest_block % data->physical_blocks);
	/* Limit the search to a small multiple of the expected erase block */
	max_search = 2 * (data->erase_size / data->block_size);
	while (max_search--) {
		rc = api->read(dev, res, 0, &temp, sizeof(temp));
		if (rc < 0) {
			return rc;
		}
		if ((temp.block_wrap != 0x00) && (temp.block_wrap != 0xFF)) {
			return 0;
		}
		data->earliest_block += 1;
		if (++res == data->physical_blocks) {
			return 0;
		}
	}
	/* This is typically seen on Nordic Development kits that ship with some
	 * data on the first flash page but nothing else.
	 */
	LOG_WRN("Data search fail (Pre-existing data on flash?)");
	return -EINVAL;
}

int data_logger_common_init(const struct device *dev)
{
	struct data_logger_common_data *data = dev->data;
	const struct data_logger_api *api = dev->api;
	uint16_t erase_blocks;
	int rc;

	sys_slist_init(&data->callbacks);

	data->bytes_logged = 0;
	data->boot_block = 0;
	data->current_block = 0;
	data->earliest_block = 0;
	data->flags = 0;

#ifdef CONFIG_DATA_LOGGER_OFFLOAD_WRITES
	{
		const struct data_logger_common_config *config = dev->config;

		if (!config->queued_writes) {
			__ASSERT(data->block_size <= BLOCK_QUEUE_MAX_SIZE,
				 "Block will not fit on queue");
		}
	}
#endif

	if (!IS_PERSISTENT_LOGGER(api)) {
		/* Wireless loggers don't need further initialisation */
		LOG_INF("Wireless logger %s", dev->name);
		return 0;
	}

	/* Find start of data */
	struct data_logger_persistent_block_header first, last;

	/* Read first and last physical blocks on the device */
	rc = api->read(dev, 0, 0, &first, sizeof(first));
	if (rc < 0) {
		return rc;
	}
	rc = api->read(dev, data->physical_blocks - 1, 0, &last, sizeof(last));
	if (rc < 0) {
		return rc;
	}

	erase_blocks = data->erase_size / data->block_size;
	if (first.block_wrap == last.block_wrap) {
		/* Either completely erased, or all blocks written with same wrap */
		if ((first.block_wrap == 0x00 || first.block_wrap == 0xFF)) {
			/* Completely erased */
			data->current_block = 0;
		} else {
			/* All blocks written with same wrap */
			data->current_block = first.block_wrap * data->physical_blocks;
			data->earliest_block = data->current_block - data->physical_blocks;
		}
	} else if ((first.block_wrap == 0x00 || first.block_wrap == 0xFF)) {
		/* First chunk has been erased after a complete write */
		data->current_block = last.block_wrap * data->physical_blocks;
		data->earliest_block = data->current_block - data->physical_blocks + erase_blocks;
	} else {
		/* Search for current block */
		rc = current_block_search(dev, first.block_wrap);
		if (rc < 0) {
			LOG_ERR("%s failed to search for current state (%d)", dev->name, rc);
			return rc;
		}
	}

	data->boot_block = data->current_block;
	LOG_INF("%s -> %u/%u blocks", dev->name, data->current_block, data->logical_blocks);
#ifdef CONFIG_DATA_LOGGER_RAM_BUFFER
	{
		const struct data_logger_common_config *config = dev->config;

		if (config->ram_buf_len) {
			LOG_INF("%s -> Extra %zu byte RAM buffer", dev->name, config->ram_buf_len);
		}
#ifdef CONFIG_DATA_LOGGER_BURST_WRITES
		if (api->write_burst) {
			__ASSERT(IS_PERSISTENT_LOGGER(api), "Expected persistent logger");
			__ASSERT(config->requires_full_block_write,
				 "Expected only full block writes");
			__ASSERT(config->ram_buf_len % data->block_size == 0,
				 "RAM buffer must be multiple of block size");
			/* To simplify the initial implementation, only support loggers that don't
			 * erase (SD)
			 */
			__ASSERT(data->logical_blocks == data->physical_blocks,
				 "Expected no wrapping");
		}
#endif /* CONFIG_DATA_LOGGER_BURST_WRITES */
	}
#endif /* CONFIG_DATA_LOGGER_RAM_BUFFER */
	return 0;
}

void data_logger_register_cb(const struct device *dev, struct data_logger_cb *cb)
{
	struct data_logger_common_data *data = dev->data;

	sys_slist_append(&data->callbacks, &cb->node);
}

void data_logger_common_block_size_changed(const struct device *dev, uint16_t block_size)
{
	struct data_logger_common_data *data = dev->data;
	struct data_logger_cb *cb;

	/* Update internal state */
	data->block_size = block_size;
	/* Notify subscribers */
	SYS_SLIST_FOR_EACH_CONTAINER(&data->callbacks, cb, node) {
		if (cb->block_size_update) {
			cb->block_size_update(dev, block_size, cb->user_data);
		}
	}
}

#ifdef CONFIG_ZTEST

void data_logger_set_erase_state(const struct device *dev, bool enabled)
{
	struct data_logger_common_data *data = dev->data;

	if (enabled) {
		data->flags |= DATA_LOGGER_FLAGS_ERASING;
	} else {
		data->flags &= ~DATA_LOGGER_FLAGS_ERASING;
	}
}

#endif /* CONFIG_ZTEST */
