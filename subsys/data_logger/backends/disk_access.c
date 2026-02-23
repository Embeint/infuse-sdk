/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#define DT_DRV_COMPAT embeint_data_logger_disk_access

#include <zephyr/device.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/logging/log.h>

#include <infuse/data_logger/logger.h>

#include "common.h"

#define DATA_LOGGER_DISK_ACCESS_MAX_WRAPS 254

struct dl_disk_access_config {
	struct data_logger_common_config common;
	const char *disk;
};
struct dl_disk_access_data {
	struct data_logger_common_data common;
	uint8_t block_buffer[CONFIG_DATA_LOGGER_DISK_ACCESS_MAX_SECTOR_SIZE] __aligned(4);
};

LOG_MODULE_REGISTER(data_logger_disk, CONFIG_DATA_LOGGER_DISK_ACCESS_LOG_LEVEL);

static int logger_disk_access_write(const struct device *dev, uint32_t phy_block,
				    enum infuse_type data_type, const void *mem, uint16_t mem_len)
{
	const struct dl_disk_access_config *config = dev->config;
	struct dl_disk_access_data *data = dev->data;

	__ASSERT(mem_len == data->common.block_size, "Not full block");

	LOG_DBG("Writing block: %08X", phy_block);

	return disk_access_write(config->disk, mem, phy_block, 1);
}

static int logger_disk_access_read(const struct device *dev, uint32_t phy_block,
				   uint16_t block_offset, void *mem, uint16_t mem_len)
{
	const struct dl_disk_access_config *config = dev->config;
	struct dl_disk_access_data *data = dev->data;
	bool aligned = ((uintptr_t)mem % sizeof(uint32_t)) == 0;
	const uint16_t block_size = data->common.block_size;
	int rc;

	LOG_DBG("Reading from logger block: %08X", phy_block);

	if (aligned && (block_offset == 0) && (mem_len == block_size)) {
		/* Read directly into provided buffer */
		rc = disk_access_read(config->disk, mem, phy_block, mem_len / block_size);
	} else {
		/* Read complete block from file */
		rc = disk_access_read(config->disk, data->block_buffer, phy_block, 1);
		/* Memcpy required data out */
		memcpy(mem, data->block_buffer + block_offset, mem_len);
	}
	return rc;
}

static int logger_disk_access_erase(const struct device *dev, uint32_t phy_block, uint32_t num)
{
	const struct dl_disk_access_config *config = dev->config;

	return disk_access_erase(config->disk, phy_block, num);
}

static int logger_disk_access_reset(const struct device *dev, uint32_t block_hint,
				    void (*erase_progress)(uint32_t blocks_erased))
{
	const uint16_t sector_chunks = CONFIG_DATA_LOGGER_DISK_ACCESS_ERASE_SECTOR_CHUNKS;
	const struct dl_disk_access_config *config = dev->config;
	struct dl_disk_access_data *data = dev->data;
	uint32_t sectors_to_erase;
	uint32_t sector = 0;
	int rc;

	sectors_to_erase = MIN(block_hint, data->common.physical_blocks);

	ARG_UNUSED(block_hint);

	while (sector < sectors_to_erase) {
		/* Erase the chunk */
		rc = disk_access_erase(config->disk, sector, sector_chunks);
		if (rc < 0) {
			return rc;
		}

		/* Update state */
		sector += sector_chunks;

		/* Run user callback */
		erase_progress(sector);
	}

	ARG_UNUSED(erase_progress);

	return disk_access_erase(config->disk, 0, data->common.physical_blocks);
}

/* Need to hook into this function when testing */
IF_DISABLED(CONFIG_ZTEST, (static))
int logger_disk_access_init(const struct device *dev)
{
	const struct dl_disk_access_config *config = dev->config;
	struct dl_disk_access_data *data = dev->data;
	uint32_t sector_count;
	uint32_t sector_size;
	uint32_t erase_blocks;
	int rc;

	rc = disk_access_ioctl(config->disk, DISK_IOCTL_CTRL_INIT, NULL);
	if (rc != 0) {
		LOG_ERR("Failed to init disk (%d)", rc);
		return rc;
	}
	rc = disk_access_ioctl(config->disk, DISK_IOCTL_GET_SECTOR_COUNT, &sector_count);
	if (rc != 0) {
		LOG_ERR("Failed to query %s (%d)", "sector count", rc);
		return rc;
	}
	rc = disk_access_ioctl(config->disk, DISK_IOCTL_GET_SECTOR_SIZE, &sector_size);
	if (rc != 0) {
		LOG_ERR("Failed to query %s (%d)", "sector size", rc);
		return rc;
	}
	if (sector_size > CONFIG_DATA_LOGGER_DISK_ACCESS_MAX_SECTOR_SIZE) {
		LOG_ERR("Insufficient block size (%d > %d)", sector_size,
			CONFIG_DATA_LOGGER_DISK_ACCESS_MAX_SECTOR_SIZE);
		return -ENOSPC;
	}
	rc = disk_access_ioctl(config->disk, DISK_IOCTL_GET_ERASE_BLOCK_SZ, &erase_blocks);
	if (rc != 0) {
		LOG_ERR("Failed to query %s (%d)", "erase block count", rc);
		return rc;
	}

	/* Setup common data structure */
	data->common.physical_blocks = sector_count;
	data->common.logical_blocks = sector_count * DATA_LOGGER_DISK_ACCESS_MAX_WRAPS;
	data->common.block_size = sector_size;
	data->common.erase_size = erase_blocks * sector_size;
	data->common.erase_val = 0xFF;

	/* Common init function */
	return data_logger_common_init(dev);
}

const struct data_logger_api data_logger_disk_access_api = {
	.write = logger_disk_access_write,
	.read = logger_disk_access_read,
	.erase = logger_disk_access_erase,
	.reset = logger_disk_access_reset,
};

#define DATA_LOGGER_DEFINE(inst)                                                                   \
	COMMON_CONFIG_PRE(inst);                                                                   \
	static struct dl_disk_access_config config##inst = {                                       \
		.common = COMMON_CONFIG_INIT(inst, true, false, 1),                                \
		.disk = DT_PROP(DT_INST_PROP(inst, disk), disk_name),                              \
	};                                                                                         \
	static struct dl_disk_access_data data##inst;                                              \
	DEVICE_DT_INST_DEFINE(inst, logger_disk_access_init, NULL, &data##inst, &config##inst,     \
			      POST_KERNEL, 80, &data_logger_disk_access_api);

#define DATA_LOGGER_DEFINE_WRAPPER(inst)                                                           \
	IF_ENABLED(DATA_LOGGER_DEPENDENCIES_MET(DT_DRV_INST(inst)), (DATA_LOGGER_DEFINE(inst)))

DT_INST_FOREACH_STATUS_OKAY(DATA_LOGGER_DEFINE_WRAPPER)
