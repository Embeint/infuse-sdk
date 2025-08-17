/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_SUBSYS_DATA_LOGGER_BACKENDS_EXFAT_COMMON_H_
#define INFUSE_SDK_SUBSYS_DATA_LOGGER_BACKENDS_EXFAT_COMMON_H_

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DATA_LOGGER_EXFAT_BLOCK_SIZE 512

#define LBA_NO_FILE      (UINT32_MAX)
#define LBA_NO_MEM       (UINT32_MAX - 1)
#define MIN_CLUSTER_SIZE 4096

struct dl_exfat_config {
	struct data_logger_common_config common;
	const char *disk;
};

struct dl_exfat_data {
	struct data_logger_common_data common;
	struct k_sem filesystem_claim;
	FATFS infuse_fatfs;
	uint8_t block_buffer[DATA_LOGGER_EXFAT_BLOCK_SIZE] __aligned(4);
	uint32_t cached_file_num;
	uint32_t cached_file_lba;
};

bool logger_exfat_filesystem_is_infuse(const struct device *dev, const char *label);

int logger_exfat_filesystem_common_init(const struct device *dev, const char *label);

void logger_exfat_disk_info_store(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_SUBSYS_DATA_LOGGER_BACKENDS_EXFAT_COMMON_H_ */
