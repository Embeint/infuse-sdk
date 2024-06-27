/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#define DT_DRV_COMPAT embeint_tdf_data_logger

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/buf.h>

#include <infuse/types.h>
#include <infuse/tdf/tdf.h>
#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/epacket/interface.h>

struct tdf_logger_config {
	const struct device *logger;
	uint8_t *tdf_buffer;
	uint16_t tdf_buffer_max_size;
};

struct tdf_logger_data {
	struct k_sem lock;
	struct tdf_buffer_state tdf_state;
	uint8_t block_overhead;
};

/* Mapping of logger bitmask */
static const struct device *logger_mapping[] = {
	[_TDF_DATA_LOGGER_SERIAL_OFFSET] = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(tdf_logger_serial)),
	[_TDF_DATA_LOGGER_UDP_OFFSET] = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(tdf_logger_udp)),
	[_TDF_DATA_LOGGER_FLASH_OFFSET] = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(tdf_logger_flash)),
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

	if (!locked) {
		/* Lock access */
		k_sem_take(&data->lock, K_FOREVER);
	}

	/* Re-add the overhead */
	net_buf_simple_push(&data->tdf_state.buf, data->block_overhead);

	/* Push data to logger */
	rc = data_logger_block_write(config->logger, INFUSE_TDF, data->tdf_state.buf.data,
				     data->tdf_state.buf.len);
	if (rc < 0) {
		LOG_ERR("%s failed to write block (%d)", dev->name, rc);
	}

	/* Reset buffer and reserve overhead */
	tdf_buffer_state_reset(&data->tdf_state);
	net_buf_simple_reserve(&data->tdf_state.buf, data->block_overhead);

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

int tdf_data_logger_log_array_dev(const struct device *dev, uint16_t tdf_id, uint8_t tdf_len,
				  uint8_t tdf_num, uint64_t time, uint16_t period, void *mem)
{
	struct tdf_logger_data *data = dev->data;
	int rc;

	k_sem_take(&data->lock, K_FOREVER);
relog:
	rc = tdf_add(&data->tdf_state, tdf_id, tdf_len, tdf_num, time, period, mem);
	if (rc == -ENOMEM) {
		LOG_DBG("%s no space, flush and retry", dev->name);
		rc = flush_internal(dev, true);
		if (rc < 0) {
			goto unlock;
		}
		goto relog;
	} else if (rc < 0) {
		LOG_WRN("%s failed to add (%d)", dev->name, rc);
		goto unlock;
	} else if (rc != tdf_num) {
		/* Only some TDFs added */
		LOG_DBG("%s logged %d/%d", dev->name, rc, tdf_num);
		mem = (uint8_t *)mem + ((uint16_t)tdf_len * rc);
		time += (period * rc);
		tdf_num -= rc;
		goto relog;
	}
	LOG_DBG("%s current offset (%d/%d)", dev->name, data->tdf_state.buf.len,
		data->tdf_state.buf.size);

	/* Auto flush if no space left for more TDFs (3 byte header + 1 byte data) */
	if (net_buf_simple_tailroom(&data->tdf_state.buf) < 4) {
		LOG_DBG("%s auto flush", dev->name);
		rc = flush_internal(dev, true);
	}

unlock:
	k_sem_give(&data->lock);
	return rc < 0 ? rc : 0;
}

void tdf_data_logger_log_array(uint8_t logger_mask, uint16_t tdf_id, uint8_t tdf_len,
			       uint8_t tdf_num, uint64_t time, uint16_t period, void *data)
{
	const struct device *dev;

	/* Flush all loggers given */
	do {
		dev = logger_mask_iter(&logger_mask);
		if (dev) {
			(void)tdf_data_logger_log_array_dev(dev, tdf_id, tdf_len, tdf_num, time,
							    period, data);
		}
	} while (dev);
}

IF_DISABLED(CONFIG_ZTEST, (static))
int tdf_data_logger_init(const struct device *dev)
{
	const struct tdf_logger_config *config = dev->config;
	struct tdf_logger_data *data = dev->data;
	struct data_logger_state logger_state;

	/* Init lock semaphore */
	k_sem_init(&data->lock, 1, 1);

	/* Link data buffer to net buf */
	net_buf_simple_init_with_data(&data->tdf_state.buf, config->tdf_buffer,
				      config->tdf_buffer_max_size);

	/* Get required overhead for message buffers */
	data_logger_get_state(config->logger, &logger_state);
	data->block_overhead = logger_state.block_overhead;

	/* Reset buffer with overhead */
	tdf_buffer_state_reset(&data->tdf_state);
	net_buf_simple_reserve(&data->tdf_state.buf, data->block_overhead);
	LOG_DBG("%s max size %d (overhead %d)\n", dev->name, data->tdf_state.buf.size,
		data->block_overhead);
	return 0;
}

#define TDF_DATA_LOGGER_DEFINE(inst)                                                               \
	static struct tdf_logger_data tdf_logger_data##inst;                                       \
	static uint8_t tdf_mem_buffer##inst[DATA_LOGGER_MAX_SIZE(DT_PARENT(DT_DRV_INST(inst)))];   \
	const struct tdf_logger_config tdf_logger_config##inst = {                                 \
		.logger = DEVICE_DT_GET(DT_PARENT(DT_DRV_INST(inst))),                             \
		.tdf_buffer = tdf_mem_buffer##inst,                                                \
		.tdf_buffer_max_size = sizeof(tdf_mem_buffer##inst),                               \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, tdf_data_logger_init, NULL, &tdf_logger_data##inst,            \
			      &tdf_logger_config##inst, POST_KERNEL, 81, NULL);

#define TDF_DATA_LOGGER_DEFINE_WRAPPER(inst)                                                       \
	IF_ENABLED(DATA_LOGGER_DEPENDENCIES_MET(DT_PARENT(DT_DRV_INST(inst))),                     \
		   (TDF_DATA_LOGGER_DEFINE(inst)))

DT_INST_FOREACH_STATUS_OKAY(TDF_DATA_LOGGER_DEFINE_WRAPPER)
