/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#define DT_DRV_COMPAT embeint_data_logger_flash_map

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>

#include <infuse/data_logger/logger.h>

#include "common.h"

#define DATA_LOGGER_FLASH_MAP_BLOCK_SIZE 512
#define DATA_LOGGER_FLASH_MAP_MAX_WRAPS  254

struct dl_flash_map_config {
	struct data_logger_common_config common;
	uint32_t physical_blocks;
	uint16_t erase_size;
	uint16_t max_block_size;
	uint8_t flash_area_id;
};
struct dl_flash_map_data {
	struct data_logger_common_data common;
	const struct flash_area *area;
};

static int logger_flash_map_write(const struct device *dev, uint32_t phy_block,
				  enum infuse_type data_type, const void *mem, uint16_t mem_len)
{
	struct dl_flash_map_data *data = dev->data;
	off_t offset = DATA_LOGGER_FLASH_MAP_BLOCK_SIZE * phy_block;

	/* Data type already encoded into the mem buffer */
	ARG_UNUSED(data_type);

	return flash_area_write(data->area, offset, mem, mem_len);
}

static int logger_flash_map_read(const struct device *dev, uint32_t phy_block,
				 uint16_t block_offset, void *mem, uint16_t mem_len)
{
	struct dl_flash_map_data *data = dev->data;
	off_t offset = (DATA_LOGGER_FLASH_MAP_BLOCK_SIZE * phy_block) + block_offset;

	return flash_area_read(data->area, offset, mem, mem_len);
}

static int logger_flash_map_erase(const struct device *dev, uint32_t phy_block, uint32_t num)
{
	struct dl_flash_map_data *data = dev->data;
	off_t offset = DATA_LOGGER_FLASH_MAP_BLOCK_SIZE * phy_block;
	size_t len = DATA_LOGGER_FLASH_MAP_BLOCK_SIZE * num;

	return flash_area_erase(data->area, offset, len);
}

static int logger_flash_map_reset(const struct device *dev, uint32_t block_hint,
				  void (*erase_progress)(uint32_t blocks_erased))
{
	struct dl_flash_map_data *data = dev->data;
	size_t remaining = DATA_LOGGER_FLASH_MAP_BLOCK_SIZE * block_hint;
	size_t complete = 0;
	off_t offset = 0;
	size_t to_erase;
	int rc;

	while (remaining) {
		/* Erase in 64kB chunks */
		to_erase = MIN(remaining, 64 * 1024);

		/* Erase the chunk */
		rc = flash_area_erase(data->area, offset, to_erase);
		if (rc < 0) {
			return rc;
		}

		/* Update state */
		complete += to_erase / DATA_LOGGER_FLASH_MAP_BLOCK_SIZE;
		offset += to_erase;
		remaining -= to_erase;

		/* Run user callback */
		erase_progress(complete);
	}
	return 0;
}

/* Need to hook into this function when testing */
IF_DISABLED(CONFIG_ZTEST, (static))
int logger_flash_map_init(const struct device *dev)
{
	const struct dl_flash_map_config *config = dev->config;
	struct dl_flash_map_data *data = dev->data;
	const struct flash_parameters *params;
	int rc;

	/* Setup common data structure */
	data->common.physical_blocks = config->physical_blocks;
	data->common.logical_blocks = config->physical_blocks * DATA_LOGGER_FLASH_MAP_MAX_WRAPS;
	data->common.block_size = config->max_block_size;
	data->common.erase_size = config->erase_size;
	data->common.erase_val = 0xFF;

	/* Open flash area */
	rc = flash_area_open(config->flash_area_id, &data->area);
	if (rc == 0) {
		params = flash_get_parameters(data->area->fa_dev);
		data->common.erase_val = params->erase_value;
		flash_area_close(data->area);
	} else {
		return -ENODEV;
	}

	/* Common init function */
	return data_logger_common_init(dev);
}

const struct data_logger_api data_logger_flash_map_api = {
	.write = logger_flash_map_write,
	.read = logger_flash_map_read,
	.erase = logger_flash_map_erase,
	.reset = logger_flash_map_reset,
};

#define DATA_LOGGER_DEFINE(inst)                                                                   \
	COMMON_CONFIG_PRE(inst);                                                                   \
	static struct dl_flash_map_config config##inst = {                                         \
		.common = COMMON_CONFIG_INIT(inst, false, false, sizeof(uint32_t)),                \
		.flash_area_id = DT_FIXED_PARTITION_ID(DT_INST_PROP(inst, partition)),             \
		.physical_blocks = (DT_REG_SIZE(DT_INST_PROP(inst, partition)) /                   \
				    DATA_LOGGER_FLASH_MAP_BLOCK_SIZE),                             \
		.erase_size = DT_PROP_OR(DT_GPARENT(DT_INST_PROP(inst, partition)),                \
					 erase_block_size, 4096),                                  \
		.max_block_size = DATA_LOGGER_FLASH_MAP_BLOCK_SIZE,                                \
	};                                                                                         \
	static struct dl_flash_map_data data##inst;                                                \
	DEVICE_DT_INST_DEFINE(inst, logger_flash_map_init, NULL, &data##inst, &config##inst,       \
			      POST_KERNEL, 80, &data_logger_flash_map_api);

#define DATA_LOGGER_DEFINE_WRAPPER(inst)                                                           \
	IF_ENABLED(DATA_LOGGER_DEPENDENCIES_MET(DT_DRV_INST(inst)), (DATA_LOGGER_DEFINE(inst)))

DT_INST_FOREACH_STATUS_OKAY(DATA_LOGGER_DEFINE_WRAPPER)
