/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#define DT_DRV_COMPAT embeint_tdf_data_logger

#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/types.h>
#include <infuse/tdf/tdf.h>
#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/epacket/interface.h>

#include "../backends/common.h"

#define DATA_GUARD_HEAD 0xb4ef00fc
#define DATA_GUARD_TAIL 0xbf696b59

#if DT_ANY_INST_HAS_BOOL_STATUS_OKAY(tdf_remote)
#define TDF_REMOTE_SUPPORT 1
#endif

struct tdf_logger_config {
	const struct device *logger;
	uint16_t tdf_buffer_max_size;
	uint8_t block_type;
};

/* Define TDF data struct with given buffer length */
#define TDF_LOGGER_DATA_TYPE(type_name, len)                                                       \
	struct type_name {                                                                         \
		uint32_t guard_head;                                                               \
		struct k_sem lock;                                                                 \
		struct tdf_buffer_state tdf_state;                                                 \
		struct data_logger_cb logger_cb;                                                   \
		IF_ENABLED(TDF_REMOTE_SUPPORT, (uint64_t remote_id;))                              \
		uint8_t full_block_write;                                                          \
		uint8_t block_overhead;                                                            \
		uint8_t tdf_buffer[len] __aligned(4);                                              \
		uint32_t guard_tail;                                                               \
	}

/* Common type */
struct tdf_logger_data {
	uint32_t guard_head;
	struct k_sem lock;
	struct tdf_buffer_state tdf_state;
	struct data_logger_cb logger_cb;
#ifdef TDF_REMOTE_SUPPORT
	uint64_t remote_id;
#endif
	uint8_t full_block_write;
	uint8_t block_overhead;
	uint8_t tdf_buffer[] __aligned(4);
};

#define GUARD_TAIL_OFFSET(len)                                                                     \
	ROUND_UP(offsetof(struct tdf_logger_data, tdf_buffer) + len, sizeof(uint32_t))

#define LOGGER_GET(node_id)                                                                        \
	COND_CODE_1(DT_NODE_HAS_STATUS(node_id, okay),                                             \
		    (COND_CODE_1(DATA_LOGGER_DEPENDENCIES_MET(DT_PARENT(node_id)),                 \
				 (DEVICE_DT_GET(node_id)), (NULL))),                               \
		    (NULL))

/* Mapping of logger bitmask */
static const struct device *logger_mapping[] = {
	[_TDF_DATA_LOGGER_FLASH_OFFSET] = LOGGER_GET(DT_NODELABEL(tdf_logger_flash)),
	[_TDF_DATA_LOGGER_REMOVABLE_OFFSET] = LOGGER_GET(DT_NODELABEL(tdf_logger_removable)),
#ifdef CONFIG_TDF_DATA_LOGGER_SERIAL_DUMMY_BACKEND
	[_TDF_DATA_LOGGER_SERIAL_OFFSET] = LOGGER_GET(DT_NODELABEL(tdf_logger_dummy)),
#else
	[_TDF_DATA_LOGGER_SERIAL_OFFSET] = LOGGER_GET(DT_NODELABEL(tdf_logger_serial)),
#endif /* CONFIG_TDF_DATA_LOGGER_SERIAL_DUMMY_BACKEND */
	[_TDF_DATA_LOGGER_UDP_OFFSET] = LOGGER_GET(DT_NODELABEL(tdf_logger_udp)),
	[_TDF_DATA_LOGGER_BT_ADV_OFFSET] = LOGGER_GET(DT_NODELABEL(tdf_logger_bt_adv)),
	[_TDF_DATA_LOGGER_BT_PERIPHERAL_OFFSET] =
		LOGGER_GET(DT_NODELABEL(tdf_logger_bt_peripheral)),
};

LOG_MODULE_REGISTER(tdf_logger, CONFIG_TDF_DATA_LOGGER_LOG_LEVEL);

/* Return next valid logger from mask */
static const struct device *logger_mask_iter(uint8_t *mask)
{
	const struct device *dev;
	uint8_t offset;

	while (*mask) {
		/* Find next set bit */
		offset = __builtin_ffs(*mask) - 1;
		if (offset >= ARRAY_SIZE(logger_mapping)) {
			return NULL;
		}
		/* Clear bit */
		*mask ^= 1 << offset;

		dev = logger_mapping[offset];
		if (dev) {
			return dev;
		}
	}
	return NULL;
}

int tdf_data_logger_block_bytes_pending(const struct device *dev)
{
	struct tdf_logger_data *data = dev->data;

	return data->tdf_state.buf.len;
}

int tdf_data_logger_block_bytes_remaining(const struct device *dev)
{
	struct tdf_logger_data *data = dev->data;

	return net_buf_simple_tailroom(&data->tdf_state.buf);
}

static int flush_internal(const struct device *dev, bool locked)
{
	const struct tdf_logger_config *config = dev->config;
	struct tdf_logger_data *data = dev->data;
	int rc;

	/* No work to do */
	if (data->tdf_state.buf.len == 0) {
		LOG_DBG("%s no data to log", dev->name);
		return 0;
	}

#ifdef TDF_REMOTE_SUPPORT
	if ((config->block_type == INFUSE_TDF_REMOTE) &&
	    (data->tdf_state.buf.len == sizeof(uint64_t))) {
		/* No data to log, but we need to update the remote ID */
		net_buf_simple_remove_le64(&data->tdf_state.buf);
		net_buf_simple_add_le64(&data->tdf_state.buf, data->remote_id);
		return 0;
	}
#endif /* TDF_REMOTE_SUPPORT */

	if (!locked) {
		/* Lock access */
		k_sem_take(&data->lock, K_FOREVER);
	}

	/* Re-add the overhead */
	net_buf_simple_push(&data->tdf_state.buf, data->block_overhead);

	/* Pad empty bytes if required */
	if (data->full_block_write) {
		uint16_t append_len = data->tdf_state.buf.size - data->tdf_state.buf.len;
		uint8_t *p = net_buf_simple_add(&data->tdf_state.buf, append_len);

		memset(p, 0xFF, append_len);
	}

	/* Push data to logger */
	rc = data_logger_block_write(config->logger, config->block_type, data->tdf_state.buf.data,
				     data->tdf_state.buf.len);
	if (rc == -ENOTCONN) {
		LOG_DBG("%s failed to write block (%d)", dev->name, rc);
	} else if (rc < 0) {
		LOG_ERR("%s failed to write block (%d)", dev->name, rc);
	}

	/* Reset buffer and reserve overhead */
	tdf_buffer_state_reset(&data->tdf_state);
	net_buf_simple_reserve(&data->tdf_state.buf, data->block_overhead);
#ifdef TDF_REMOTE_SUPPORT
	if (config->block_type == INFUSE_TDF_REMOTE) {
		net_buf_simple_add_le64(&data->tdf_state.buf, data->remote_id);
	}
#endif /* TDF_REMOTE_SUPPORT */

	if (!locked) {
		/* Unlock access */
		k_sem_give(&data->lock);
	}
	return rc;
}

int tdf_data_logger_flush_dev(const struct device *dev)
{
	return flush_internal(dev, false);
}

void tdf_data_logger_flush(uint8_t logger_mask)
{
	const struct device *dev;

	/* Flush all loggers given */
	do {
		dev = logger_mask_iter(&logger_mask);
		if (dev) {
			(void)tdf_data_logger_flush_dev(dev);
		}
	} while (dev);
}

#ifdef TDF_REMOTE_SUPPORT
int tdf_data_logger_remote_id_set(const struct device *dev, uint64_t remote_id)
{
	const struct tdf_logger_config *config = dev->config;
	struct tdf_logger_data *data = dev->data;
	int rc;

	if (config->block_type != INFUSE_TDF_REMOTE) {
		return -EINVAL;
	}

	if (data->remote_id == remote_id) {
		/* ID hasn't changed, don't flush */
		return 0;
	}

	k_sem_take(&data->lock, K_FOREVER);
	/* Update remote before flushing so next block is setup with correct value */
	data->remote_id = remote_id;
	rc = flush_internal(dev, true);
	k_sem_give(&data->lock);
	return rc;
}
#endif /* TDF_REMOTE_SUPPORT */

static int log_locked(const struct device *dev, uint16_t tdf_id, uint8_t tdf_len, uint8_t tdf_num,
		      enum tdf_data_format format, uint64_t time, uint32_t idx_period,
		      const void *mem)
{
	struct tdf_logger_data *data = dev->data;
	int rc;

relog:
	rc = tdf_add_core(&data->tdf_state, tdf_id, tdf_len, tdf_num, time, idx_period, mem,
			  format);
	if (rc == -ENOMEM) {
		LOG_DBG("%s no space, flush and retry", dev->name);
		rc = flush_internal(dev, true);
		if (rc < 0) {
			return rc;
		}
		goto relog;
	} else if (rc < 0) {
		LOG_WRN("%s failed to add (%d)", dev->name, rc);
		return rc;
	} else if (rc != tdf_num) {
		/* Only some TDFs added */
		LOG_DBG("%s logged %d/%d", dev->name, rc, tdf_num);
		mem = (uint8_t *)mem + ((uint16_t)tdf_len * rc);
		if (format == TDF_DATA_FORMAT_IDX_ARRAY) {
			/* Next loop has an updated start index and no time */
			idx_period += rc;
			time = 0;
		} else {
			time += (idx_period * rc);
		}
		tdf_num -= rc;
		/* Logging precomputed diffs from a point other that the start is currently
		 * not supported.
		 */
		if (!(format & TDF_DATA_FORMAT_DIFF_PRECOMPUTED)) {
			goto relog;
		}
	}
	LOG_DBG("%s current offset (%d/%d)", dev->name, data->tdf_state.buf.len,
		data->tdf_state.buf.size);

	/* Auto flush if no space left for more TDFs (3 byte header + 1 byte data) */
	if (net_buf_simple_tailroom(&data->tdf_state.buf) < 4) {
		LOG_DBG("%s auto flush", dev->name);
		rc = flush_internal(dev, true);
	}
	return rc;
}

int tdf_data_logger_log_core_dev(const struct device *dev, uint16_t tdf_id, uint8_t tdf_len,
				 uint8_t tdf_num, enum tdf_data_format format, uint64_t time,
				 uint32_t idx_period, const void *mem)
{
	const struct tdf_logger_config *config = dev->config;
	struct tdf_logger_data *data = dev->data;
	int rc;

	/* Validate logger initialised correctly */
	if (!device_is_ready(config->logger)) {
		LOG_WRN_ONCE("%s backend failed to initialise", dev->name);
		return -ENODEV;
	}

	/* Logging to disconnect backend is not possible */
	if (data->tdf_state.buf.size == 0) {
		LOG_DBG("%s currently disconnected", dev->name);
		return -ENOTCONN;
	}

	/* Logging on the system workqueue can cause deadlocks and should be avoided */
	if (k_current_get() == k_work_queue_thread_get(&k_sys_work_q)) {
		LOG_WRN("%s logging on system workqueue", dev->name);
	}

	k_sem_take(&data->lock, K_FOREVER);
	rc = log_locked(dev, tdf_id, tdf_len, tdf_num, format, time, idx_period, mem);
	k_sem_give(&data->lock);
	return rc < 0 ? rc : 0;
}

void tdf_data_logger_log_core(uint8_t logger_mask, uint16_t tdf_id, uint8_t tdf_len,
			      uint8_t tdf_num, enum tdf_data_format format, uint64_t time,
			      uint32_t idx_period, const void *data)
{
	const struct device *dev;

	/* Flush all loggers given */
	do {
		dev = logger_mask_iter(&logger_mask);
		if (dev) {
			(void)tdf_data_logger_log_core_dev(dev, tdf_id, tdf_len, tdf_num, format,
							   time, idx_period, data);
		}
	} while (dev);
}

#ifdef CONFIG_ZTEST
void tdf_data_logger_lock(const struct device *dev)
{
	struct tdf_logger_data *data = dev->data;

	k_sem_take(&data->lock, K_FOREVER);
}

#endif /* CONFIG_ZTEST */

static void tdf_block_size_update(const struct device *logger, uint16_t block_size, void *user_data)
{
	const struct device *dev = user_data;
	const struct tdf_logger_config *config = dev->config;
	struct tdf_logger_data *data = dev->data;
	uint16_t limited = MIN(block_size, config->tdf_buffer_max_size);
	bool is_diff;
	uint8_t num;

	k_sem_take(&data->lock, K_FOREVER);
	LOG_DBG("%s: from %d to %d bytes", dev->name, data->tdf_state.buf.size, limited);
	if (block_size == 0) {
		/* Backend disconnected, revert to using all RAM for buffering */
		data->tdf_state.buf.size = config->tdf_buffer_max_size;
	} else if (data->tdf_state.buf.len <= limited) {
		/* Updated buffer size is larger than pending data, no problems */
		data->tdf_state.buf.size = limited;
	} else {
		/* More data pending than the current buffer size */
		struct tdf_buffer_state state;
		struct tdf_parsed tdf;

		/* Snapshot the buffer state */
		tdf_parse_start(&state, data->tdf_state.buf.data, data->tdf_state.buf.len);
		/* Reset the loggers knowledge of pending data */
		data->tdf_state.buf.size = limited;
		tdf_buffer_state_reset(&data->tdf_state);
		net_buf_simple_reserve(&data->tdf_state.buf, data->block_overhead);
#ifdef TDF_REMOTE_SUPPORT
		if (config->block_type == INFUSE_TDF_REMOTE) {
			net_buf_simple_add_le64(&data->tdf_state.buf, data->remote_id);
		}
#endif /* TDF_REMOTE_SUPPORT */
		/* Re-log pending TDF's into the same buffer, which will flush as appropriate */
		while (tdf_parse(&state, &tdf) == 0) {
			is_diff = (tdf.data_type == TDF_DATA_FORMAT_DIFF_ARRAY_16_8) ||
				  (tdf.data_type == TDF_DATA_FORMAT_DIFF_ARRAY_32_8) ||
				  (tdf.data_type == TDF_DATA_FORMAT_DIFF_ARRAY_32_16);
			num = is_diff ? 1 + tdf.diff_info.num : tdf.tdf_num;
			if (is_diff) {
				tdf.data_type |= TDF_DATA_FORMAT_DIFF_PRECOMPUTED;
			}

			(void)log_locked(dev, tdf.tdf_id, tdf.tdf_len, num, tdf.data_type, tdf.time,
					 tdf.period, tdf.data);
		}
	}
	k_sem_give(&data->lock);
}

static bool tdf_data_logger_valid_data_on_buffer(const struct device *dev,
						 struct data_logger_state *logger_state)
{
	const struct tdf_logger_config *config = dev->config;
	struct tdf_logger_data *data = dev->data;
	uint32_t *guard_tail =
		(uint32_t *)(((uint8_t *)data) + GUARD_TAIL_OFFSET(config->tdf_buffer_max_size));

	/* Detect if we have just rebooted and there is potentially valid data on the buffers
	 * to recover. The conditions that must pass:
	 *
	 * 1. Data guards match magic values
	 * 2. Buffer config matches expected values
	 * 3. Buffer reports more than 0 bytes contained
	 * 4. Lock is not locked
	 * 5. Remote ID matches and populated (if enabled)
	 * 6. After parsing the buffer with `tdf_parse`:
	 *    a. Buffer offset matches recovered offset
	 *    b. Buffer timestamp matches recovered timestamp
	 */

	/* Check 1 */
	if ((data->guard_head != DATA_GUARD_HEAD) || (*guard_tail != DATA_GUARD_TAIL)) {
		return false;
	}
	/* Checks 2 */
	if (data->tdf_state.buf.size > config->tdf_buffer_max_size) {
		return false;
	}
	if (data->tdf_state.buf.__buf != data->tdf_buffer) {
		return false;
	}
	if (data->full_block_write != logger_state->requires_full_block_write) {
		return false;
	}
	if (data->block_overhead != logger_state->block_overhead) {
		return false;
	}
	/* Check 3 */
	if (data->tdf_state.buf.len == 0) {
		return false;
	}
	/* Check 4 */
	if (data->lock.count != 1) {
		return false;
	}
#ifdef TDF_REMOTE_SUPPORT
	/* Check 5 */
	if (config->block_type == INFUSE_TDF_REMOTE) {
		if (data->tdf_state.buf.len <= sizeof(data->remote_id)) {
			return false;
		}
		if (sys_get_le64(data->tdf_state.buf.data) != data->remote_id) {
			return false;
		}
	}
#endif /* TDF_REMOTE_SUPPORT */

	LOG_DBG("Checking validity of recovered buffer %d", data->tdf_state.buf.len);
	struct tdf_buffer_state state;
	struct tdf_parsed parsed, last = {0};
	uint8_t offset = 0;

#ifdef TDF_REMOTE_SUPPORT
	if (config->block_type == INFUSE_TDF_REMOTE) {
		offset = sizeof(data->remote_id);
	}
#endif /* TDF_REMOTE_SUPPORT */

	/* Parse the complete buffer */
	tdf_parse_start(&state, data->tdf_state.buf.data + offset,
			data->tdf_state.buf.len - offset);
	while (tdf_parse(&state, &parsed) == 0) {
		last = parsed;
	}

	/* Check 5a and 5b conditions */
	if ((last.time == data->tdf_state.time) && (state.buf.len == 0)) {
		return true;
	}
	return false;
}

IF_DISABLED(CONFIG_ZTEST, (static))
int tdf_data_logger_init(const struct device *dev)
{
	const struct tdf_logger_config *config = dev->config;
	struct tdf_logger_data *data = dev->data;
	struct data_logger_state logger_state;
	uint32_t *guard_tail =
		(uint32_t *)(((uint8_t *)data) + GUARD_TAIL_OFFSET(config->tdf_buffer_max_size));
	bool recovered = false;
	uint16_t size;

	/* Get required overhead for message buffers */
	data_logger_get_state(config->logger, &logger_state);

	/* Register for callbacks (memset due to __noinit) */
	memset(&data->logger_cb, 0x00, sizeof(data->logger_cb));
	data->logger_cb.block_size_update = tdf_block_size_update;
	data->logger_cb.user_data = (void *)dev;
	data_logger_register_cb(config->logger, &data->logger_cb);

	/* Check if there is valid data sitting in RAM */
	recovered = tdf_data_logger_valid_data_on_buffer(dev, &logger_state);

	/* Uncondtionally reset lock semaphore */
	k_sem_init(&data->lock, 1, 1);

	if (!recovered) {
		/* Set data guards as valid */
		data->guard_head = DATA_GUARD_HEAD;
		*guard_tail = DATA_GUARD_TAIL;

		/* Set block overhead */
		data->block_overhead = logger_state.block_overhead;
		data->full_block_write = logger_state.requires_full_block_write;

		/* If the backend is currently disconnected, allow buffering in RAM using
		 * all the available storage until the backend is connected.
		 */
		size = logger_state.block_size == 0 ? config->tdf_buffer_max_size
						    : logger_state.block_size;

		/* Link data buffer to net buf */
		net_buf_simple_init_with_data(&data->tdf_state.buf, data->tdf_buffer, size);
		/* Reset buffer with overhead */
		tdf_buffer_state_reset(&data->tdf_state);
		net_buf_simple_reserve(&data->tdf_state.buf, data->block_overhead);
#ifdef TDF_REMOTE_SUPPORT
		if (config->block_type == INFUSE_TDF_REMOTE) {
			net_buf_simple_add_le64(&data->tdf_state.buf, data->remote_id);
		}
#endif /* TDF_REMOTE_SUPPORT */
	}
	LOG_DBG("%s max size %d (overhead %d)", dev->name, config->tdf_buffer_max_size,
		data->block_overhead);
	if (recovered) {
		LOG_INF("%s recovered %d bytes over reboot", dev->name, data->tdf_state.buf.len);
		if (logger_state.block_size == 0) {
			/* Backend currently disconnected, revert to maximum size */
			data->tdf_state.buf.size = config->tdf_buffer_max_size;
		}
	}
	return 0;
}

#define TDF_DATA_LOGGER_DEFINE(inst)                                                               \
	TDF_LOGGER_DATA_TYPE(tdf_logger_data_t_##inst,                                             \
			     DATA_LOGGER_MAX_SIZE(DT_PARENT(DT_DRV_INST(inst))));                  \
	BUILD_ASSERT(offsetof(struct tdf_logger_data_t_##inst, guard_tail) ==                      \
		     GUARD_TAIL_OFFSET(DATA_LOGGER_MAX_SIZE(DT_PARENT(DT_DRV_INST(inst)))));       \
	static struct tdf_logger_data_t_##inst tdf_logger_data##inst __noinit;                     \
	const struct tdf_logger_config tdf_logger_config##inst = {                                 \
		.logger = DEVICE_DT_GET(DT_PARENT(DT_DRV_INST(inst))),                             \
		.tdf_buffer_max_size = sizeof(tdf_logger_data##inst.tdf_buffer),                   \
		.block_type = COND_CODE_1(DT_INST_PROP(inst, tdf_remote), (INFUSE_TDF_REMOTE),     \
					  (INFUSE_TDF)),                                           \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, tdf_data_logger_init, NULL, &tdf_logger_data##inst,            \
			      &tdf_logger_config##inst, POST_KERNEL, 81, NULL);

#define TDF_DATA_LOGGER_DEFINE_WRAPPER(inst)                                                       \
	IF_ENABLED(DATA_LOGGER_DEPENDENCIES_MET(DT_PARENT(DT_DRV_INST(inst))),                     \
		   (TDF_DATA_LOGGER_DEFINE(inst)))

DT_INST_FOREACH_STATUS_OKAY(TDF_DATA_LOGGER_DEFINE_WRAPPER)
