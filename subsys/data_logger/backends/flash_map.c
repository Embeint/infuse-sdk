/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#define DT_DRV_COMPAT embeint_data_logger_flash_map

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>

#include "backend_api.h"
#include "flash_map.h"

static int logger_flash_map_write(const struct data_logger_backend_config *backend, uint32_t phy_block,
				  enum infuse_type data_type, const void *mem, uint16_t mem_len)
{
	struct data_logger_backend_data *data = backend->data;
	off_t offset = DATA_LOGGER_FLASH_MAP_BLOCK_SIZE * phy_block;
	uint8_t *ptr = (uint8_t *)mem;

	/* Data type already encoded into the mem buffer */
	ARG_UNUSED(data_type);

	/* Ensure writes are word aligned */
	while (mem_len % sizeof(uint32_t)) {
		ptr[mem_len++] = data->erase_val;
	}

	return flash_area_write(data->area, offset, mem, mem_len);
}

static int logger_flash_map_read(const struct data_logger_backend_config *backend, uint32_t phy_block,
				 uint16_t block_offset, void *mem, uint16_t mem_len)
{
	struct data_logger_backend_data *data = backend->data;
	off_t offset = (DATA_LOGGER_FLASH_MAP_BLOCK_SIZE * phy_block) + block_offset;

	return flash_area_read(data->area, offset, mem, mem_len);
}

static int logger_flash_map_erase(const struct data_logger_backend_config *backend, uint32_t phy_block, uint32_t num)
{
	struct data_logger_backend_data *data = backend->data;
	off_t offset = DATA_LOGGER_FLASH_MAP_BLOCK_SIZE * phy_block;
	size_t len = DATA_LOGGER_FLASH_MAP_BLOCK_SIZE * num;

	return flash_area_erase(data->area, offset, len);
}

static int logger_flash_map_init(const struct data_logger_backend_config *backend)
{
	struct data_logger_backend_data *data = backend->data;
	const struct flash_parameters *params;
	int rc;

	/* Fixed block size */
	data->block_size = backend->max_block_size;
	data->erase_val = 0xFF;

	/* Open flash area */
	rc = flash_area_open(backend->flash_area_id, &data->area);
	if (rc == 0) {
		params = flash_get_parameters(data->area->fa_dev);
		data->erase_val = params->erase_value;
	}
	return rc;
}

const struct data_logger_backend_api data_logger_flash_map_api = {
	.init = logger_flash_map_init,
	.write = logger_flash_map_write,
	.read = logger_flash_map_read,
	.erase = logger_flash_map_erase,
};
