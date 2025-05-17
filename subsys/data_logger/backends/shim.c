/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#define DT_DRV_COMPAT embeint_data_logger_shim

#include <string.h>

#include <zephyr/device.h>

#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/backend/shim.h>

#include "common.h"

struct dl_shim_config {
	struct data_logger_common_config common;
	uint32_t physical_blocks;
};

struct dl_shim_data {
	struct data_logger_common_data common;
	struct data_logger_shim_function_data func;
};

static int logger_shim_write(const struct device *dev, uint32_t phy_block,
			     enum infuse_type data_type, const void *mem, uint16_t mem_len)
{
	struct dl_shim_data *data = dev->data;

	data->func.write.num_calls += 1;
	data->func.write.data_type = data_type;
	data->func.write.block = phy_block;
	data->func.write.data_len = mem_len;
	return data->func.write.rc;
}

static int logger_shim_read(const struct device *dev, uint32_t phy_block, uint16_t block_offset,
			    void *mem, uint16_t mem_len)
{
	struct dl_shim_data *data = dev->data;

	memset(mem, 0x00, mem_len);

	data->func.read.num_calls += 1;
	data->func.read.block = phy_block;
	data->func.read.data_len = mem_len;
	return data->func.read.rc;
}

static int logger_shim_erase(const struct device *dev, uint32_t phy_block, uint32_t num)
{
	struct dl_shim_data *data = dev->data;

	data->func.erase.num_calls += 1;
	data->func.erase.phy_block = phy_block;
	data->func.erase.num = num;
	if (data->func.erase.block_until) {
		k_sem_take(data->func.erase.block_until, K_FOREVER);
	}
	return data->func.erase.rc;
}

static int logger_shim_reset(const struct device *dev, uint32_t block_hint,
			     void (*erase_progress)(uint32_t blocks_erased))
{
	struct dl_shim_data *data = dev->data;

	data->func.reset.num_calls += 1;
	data->func.reset.block_hint = block_hint;
	if (data->func.reset.block_until) {
		k_sem_take(data->func.reset.block_until, K_FOREVER);
	}
	return data->func.reset.rc;
}

/* Need to hook into this function when testing */
int logger_shim_init(const struct device *dev)
{
	const struct dl_shim_config *config = dev->config;
	struct dl_shim_data *data = dev->data;

	/* Setup common data structure */
	data->common.physical_blocks = config->physical_blocks;
	data->common.logical_blocks = config->physical_blocks * 2;
	data->common.block_size = 512;
	data->common.erase_size = 512;
	data->common.erase_val = 0xFF;

	/* Reset state */
	data->func.write.num_calls = 0;
	data->func.read.num_calls = 0;
	data->func.erase.num_calls = 0;
	data->func.reset.num_calls = 0;
	data->func.write.rc = 0;
	data->func.read.rc = 0;
	data->func.erase.rc = 0;
	data->func.reset.rc = 0;
	data->func.erase.block_until = NULL;
	data->func.reset.block_until = NULL;

	/* Common init function */
	return data_logger_common_init(dev);
}

struct data_logger_shim_function_data *
data_logger_backend_shim_data_pointer(const struct device *dev)
{
	struct dl_shim_data *data = dev->data;

	return &data->func;
}

const struct data_logger_api data_logger_shim_api = {
	.write = logger_shim_write,
	.read = logger_shim_read,
	.erase = logger_shim_erase,
	.reset = logger_shim_reset,
};

#define DATA_LOGGER_DEFINE(inst)                                                                   \
	COMMON_CONFIG_PRE(inst);                                                                   \
	static struct dl_shim_config config##inst = {                                              \
		.common = COMMON_CONFIG_INIT(inst, false, false),                                  \
		.physical_blocks = DT_INST_PROP(inst, physical_blocks),                            \
	};                                                                                         \
	static struct dl_shim_data data##inst;                                                     \
	DEVICE_DT_INST_DEFINE(inst, logger_shim_init, NULL, &data##inst, &config##inst,            \
			      POST_KERNEL, 80, &data_logger_shim_api);

#define DATA_LOGGER_DEFINE_WRAPPER(inst)                                                           \
	IF_ENABLED(DATA_LOGGER_DEPENDENCIES_MET(DT_DRV_INST(inst)), (DATA_LOGGER_DEFINE(inst)))

DT_INST_FOREACH_STATUS_OKAY(DATA_LOGGER_DEFINE_WRAPPER)
