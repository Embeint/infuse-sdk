/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_SUBSYS_DATA_LOGGER_BACKENDS_EXFAT_H_
#define INFUSE_SDK_SUBSYS_DATA_LOGGER_BACKENDS_EXFAT_H_

#include <stdint.h>

#include <zephyr/devicetree.h>

#include <infuse/epacket/interface.h>

#include "backend_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DATA_LOGGER_EXFAT_BLOCK_SIZE 512

#define _DL_EXFAT_BLOCKS_FLASHDISK(disk_node)                                                      \
	COND_CODE_1(DT_NODE_HAS_COMPAT(disk_node, zephyr_flash_disk),                              \
		    (DT_REG_SIZE(DT_PROP(disk_node, partition)) / DATA_LOGGER_EXFAT_BLOCK_SIZE),   \
		    ())

#define _DL_EXFAT_BLOCKS_RAMDISK(disk_node)                                                        \
	COND_CODE_1(DT_NODE_HAS_COMPAT(disk_node, zephyr_ram_disk),                                \
		    (DT_PROP(disk_node, sector_count)), ())

#define _DL_EXFAT_BLOCKS_SDMMC(disk_node)                                                          \
	COND_CODE_1(DT_NODE_HAS_COMPAT(disk_node, zephyr_sdmmc_disk), (UINT32_MAX), ())

#define _DL_EXFAT_BLOCKS(disk_node)                                                                \
	(_DL_EXFAT_BLOCKS_FLASHDISK(disk_node) _DL_EXFAT_BLOCKS_RAMDISK(disk_node)                 \
		 _DL_EXFAT_BLOCKS_SDMMC(disk_node))

#define DATA_LOGGER_BACKEND_CONFIG_EXFAT(node, data_ptr)                                           \
	{                                                                                          \
		.api = &data_logger_exfat_api, .data = data_ptr,                                   \
		.disk = DT_PROP(DT_PROP(node, disk), disk_name),                                   \
		.logical_blocks = _DL_EXFAT_BLOCKS(DT_PROP(node, disk)),                           \
		.physical_blocks = _DL_EXFAT_BLOCKS(DT_PROP(node, disk)),                          \
		.erase_size = DATA_LOGGER_EXFAT_BLOCK_SIZE,                                        \
		.max_block_size = DATA_LOGGER_EXFAT_BLOCK_SIZE,                                    \
	}

extern const struct data_logger_backend_api data_logger_exfat_api;

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_SUBSYS_DATA_LOGGER_BACKENDS_EXFAT_H_ */
