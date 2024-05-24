/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#define DT_DRV_COMPAT embeint_data_logger

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include <infuse/data_logger/logger.h>

#include "backends/backend_api.h"
#include "backends/flash_map.h"
#include "backends/epacket.h"

#define IS_PERSISTENT_LOGGER(config) (config->backend.api->read != NULL)

struct data_logger_config {
	struct data_logger_backend_config backend;
};

struct data_logger_data {
	struct data_logger_backend_data backend_data;
	uint32_t current_block;
	uint32_t earliest_block;
};

LOG_MODULE_REGISTER(data_logger, CONFIG_DATA_LOGGER_LOG_LEVEL);

void data_logger_get_state(const struct device *dev, struct data_logger_state *state)
{
	const struct data_logger_config *config = dev->config;
	struct data_logger_data *data = dev->data;

	state->logical_blocks = config->backend.logical_blocks;
	state->physical_blocks = config->backend.physical_blocks;
	state->current_block = data->current_block;
	state->earliest_block = data->earliest_block;
	state->block_size = data->backend_data.block_size;
	state->block_overhead = config->backend.api->read == NULL
					? 0
					: sizeof(struct data_logger_persistent_block_header);
	state->erase_unit = config->backend.erase_size;
}

int data_logger_block_write(const struct device *dev, enum infuse_type type, void *block,
			    uint16_t block_len)
{
	const struct data_logger_config *config = dev->config;
	struct data_logger_data *data = dev->data;
	uint16_t erase_blocks = config->backend.erase_size / config->backend.max_block_size;
	uint32_t phy_block = data->current_block % config->backend.physical_blocks;
	int rc;

	/* Validate block length */
	if (block_len > data->backend_data.block_size) {
		return -EINVAL;
	}
	/* Check there is still space on the logger */
	if (data->current_block >= config->backend.logical_blocks) {
		return -ENOMEM;
	}

	LOG_DBG("%s writing to logical block %d (Phy block %d)", dev->name, data->current_block,
		phy_block);
	/* Erase next chunk if required */
	if ((data->current_block >= config->backend.physical_blocks) &&
	    ((data->current_block % erase_blocks) == 0)) {
		LOG_DBG("%s preparing block for write", dev->name);
		rc = config->backend.api->erase(&config->backend, phy_block, erase_blocks);
		if (rc < 0) {
			LOG_ERR("%s failed to prepare block (%d)", dev->name, rc);
			return rc;
		}
		/* Old data is no longer present */
		data->earliest_block += erase_blocks;
	}

	/* Add persistent block header if required */
	if (IS_PERSISTENT_LOGGER(config)) {
		struct data_logger_persistent_block_header *header = block;

		header->block_type = type;
		header->block_wrap = (data->current_block / config->backend.physical_blocks) + 1;
	}

	/* Write block to backend */
	rc = config->backend.api->write(&config->backend, phy_block, type, block, block_len);
	if (rc < 0) {
		LOG_ERR("%s failed to write to backend", dev->name);
		return rc;
	}
	data->current_block += 1;
	return 0;
}

int data_logger_block_read(const struct device *dev, uint32_t block_idx, uint16_t block_offset,
			   void *block, uint16_t block_len)
{
	const struct data_logger_config *config = dev->config;
	struct data_logger_data *data = dev->data;
	uint32_t phy_block = block_idx % config->backend.physical_blocks;
	uint32_t end_logical =
		((config->backend.max_block_size * block_idx) + block_offset + block_len - 1) /
		config->backend.max_block_size;
	uint32_t end_phy = end_logical % config->backend.physical_blocks;
	uint32_t second_read = 0;
	int rc;

	/* Can only read from persistent loggers */
	if (!IS_PERSISTENT_LOGGER(config)) {
		return -ENOTSUP;
	}

	/* Data that does not exist */
	if ((block_idx < data->earliest_block) || (end_logical >= data->current_block) ||
	    (block_offset >= config->backend.max_block_size)) {
		return -ENOENT;
	}

	/* Read goes across the wrap boundary */
	if (end_phy < phy_block) {
		uint32_t bytes_to_wrap = (config->backend.physical_blocks - phy_block) *
						 config->backend.max_block_size -
					 block_offset;

		LOG_DBG("%s read wraps across boundary after %d bytes", dev->name, bytes_to_wrap);
		second_read = block_len - bytes_to_wrap;
		block_len -= second_read;
	}

	/* Read block from backend */
	rc = config->backend.api->read(&config->backend, phy_block, block_offset, block, block_len);
	if (rc < 0) {
		LOG_ERR("%s failed to read from backend", dev->name);
	}
	/* Read data remaining after wrap */
	if (second_read) {
		block = (uint8_t *)block + block_len;
		LOG_DBG("%s reading remaining %d bytes", dev->name, second_read);
		rc = config->backend.api->read(&config->backend, 0, 0, block, second_read);
	}
	return rc;
}

static int current_block_search(const struct device *dev, uint8_t counter)
{
	const struct data_logger_config *config = dev->config;
	struct data_logger_data *data = dev->data;
	struct data_logger_persistent_block_header temp;
	uint32_t high = config->backend.physical_blocks - 1;
	uint32_t low = 0;
	uint32_t mid, res = 0;
	int rc;

	/* Binary search for last block where block_wrap == counter */
	while (low <= high) {
		mid = (low + high) / 2;
		rc = config->backend.api->read(&config->backend, mid, 0, &temp, sizeof(temp));
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
	data->current_block = ((counter - 1) * config->backend.physical_blocks) + res + 1;

	/* Find next block with valid data */
	data->earliest_block = data->current_block - config->backend.physical_blocks;
	res = (data->earliest_block % config->backend.physical_blocks);
	while (true) {
		rc = config->backend.api->read(&config->backend, res, 0, &temp, sizeof(temp));
		if (rc < 0) {
			return rc;
		}
		if ((temp.block_wrap != 0x00) && (temp.block_wrap != 0xFF)) {
			break;
		}
		data->earliest_block += 1;
		if (++res == config->backend.physical_blocks) {
			break;
		}
	}
	return 0;
}

/* Need to hook into this function when testing */
IF_DISABLED(CONFIG_ZTEST, (static))
int data_logger_init(const struct device *dev)
{
	const struct data_logger_config *config = dev->config;
	struct data_logger_data *data = dev->data;
	uint16_t erase_blocks;
	int rc;

	if (config->backend.api == NULL) {
		LOG_ERR("%s no backend", dev->name);
		return -ENODEV;
	}

	/* Initialise backend */
	rc = config->backend.api->init(&config->backend);
	if (rc < 0) {
		LOG_ERR("%s failed to init (%d)", dev->name, rc);
		return rc;
	}

	data->current_block = 0;
	data->earliest_block = 0;

	if (!IS_PERSISTENT_LOGGER(config)) {
		/* Wireless loggers don't need further initialisation */
		LOG_INF("Wireless logger %s", dev->name);
		return 0;
	}

	/* Find start of data */
	struct data_logger_persistent_block_header first, last;

	/* Read first and last physical blocks on the device */
	rc = config->backend.api->read(&config->backend, 0, 0, &first, sizeof(first));
	if (rc < 0) {
		return rc;
	}
	rc = config->backend.api->read(&config->backend, config->backend.physical_blocks - 1, 0,
				       &last, sizeof(last));
	if (rc < 0) {
		return rc;
	}

	erase_blocks = config->backend.erase_size / config->backend.max_block_size;
	if (first.block_wrap == last.block_wrap) {
		/* Either completely erased, or all blocks written with same wrap */
		if ((first.block_wrap == 0x00 || first.block_wrap == 0xFF)) {
			/* Completely erased */
			data->current_block = 0;
		} else {
			/* All blocks written with same wrap */
			data->current_block = first.block_wrap * config->backend.physical_blocks;
			data->earliest_block =
				data->current_block - config->backend.physical_blocks;
		}
	} else if ((first.block_wrap == 0x00 || first.block_wrap == 0xFF)) {
		/* First chunk has been erased after a complete write */
		data->current_block = last.block_wrap * config->backend.physical_blocks;
		data->earliest_block =
			data->current_block - config->backend.physical_blocks + erase_blocks;
	} else {
		/* Search for current block */
		rc = current_block_search(dev, first.block_wrap);
		if (rc < 0) {
			LOG_ERR("%s failed to search for current state (%d)", dev->name, rc);
			return rc;
		}
	}

	LOG_INF("%s -> %d/%d blocks", dev->name, data->current_block,
		config->backend.logical_blocks);
	return 0;
}

#define DATA_LOGGER_FLASH_MAP(inst)                                                                \
	IF_ENABLED(DT_NODE_HAS_COMPAT(DT_DRV_INST(inst), embeint_data_logger_flash_map),           \
		   (DATA_LOGGER_BACKEND_CONFIG_FLASH_MAP(DT_DRV_INST(inst),                        \
							 &data##inst.backend_data)))

#define DATA_LOGGER_EPACKET(inst)                                                                  \
	IF_ENABLED(                                                                                \
		DT_NODE_HAS_COMPAT(DT_DRV_INST(inst), embeint_data_logger_epacket),                \
		(DATA_LOGGER_BACKEND_CONFIG_EPACKET(DT_DRV_INST(inst), &data##inst.backend_data)))

#define DATA_LOGGER_BACKEND_CONFIG(inst) DATA_LOGGER_FLASH_MAP(inst) DATA_LOGGER_EPACKET(inst)

#define DATA_LOGGER_DEFINE(inst)                                                                   \
	static struct data_logger_data data##inst;                                                 \
	static struct data_logger_config config##inst = {                                          \
		.backend = DATA_LOGGER_BACKEND_CONFIG(inst),                                       \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, data_logger_init, NULL, &data##inst, &config##inst,            \
			      POST_KERNEL, 80, NULL);

DT_INST_FOREACH_STATUS_OKAY(DATA_LOGGER_DEFINE)
