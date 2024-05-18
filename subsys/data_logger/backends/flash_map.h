/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_BACKENDS_FLASH_MAP_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_BACKENDS_FLASH_MAP_H_

#include "backend_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DATA_LOGGER_FLASH_MAP_BLOCK_SIZE 512
#define DATA_LOGGER_FLASH_MAP_MAX_WRAPS  254

#define DATA_LOGGER_BACKEND_CONFIG_FLASH_MAP(node, data_ptr)                                                           \
	.backend_api = &data_logger_flash_map_api,                                                                     \
	.backend_config = {                                                                                            \
		.data = data_ptr,                                                                                      \
		.flash_area_id = DT_FIXED_PARTITION_ID(DT_PROP(node, partition)),                                      \
		.logical_blocks = (DT_REG_SIZE(DT_PROP(node, partition)) / DATA_LOGGER_FLASH_MAP_BLOCK_SIZE) *         \
				  DATA_LOGGER_FLASH_MAP_MAX_WRAPS,                                                     \
		.physical_blocks = (DT_REG_SIZE(DT_PROP(node, partition)) / DATA_LOGGER_FLASH_MAP_BLOCK_SIZE),         \
		.erase_size = DT_PROP_OR(DT_GPARENT(DT_PROP(node, partition)), erase_block_size, 4096),                \
		.max_block_size = DATA_LOGGER_FLASH_MAP_BLOCK_SIZE,                                                    \
	},

extern const struct data_logger_backend_api data_logger_flash_map_api;

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_BACKENDS_FLASH_MAP_H_ */
