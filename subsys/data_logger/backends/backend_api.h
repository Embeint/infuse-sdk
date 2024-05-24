/**
 * @file
 * @brief Data logger backend
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 *
 * @details
 * Backend API for the data logger abstraction
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_BACKEND_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_BACKEND_H_

#include <stdint.h>

#include <zephyr/toolchain.h>

#include <infuse/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Data logger backend API
 * @defgroup data_logger_backend_apis Data logger backend APIs
 * @{
 */

struct data_logger_backend_data {
	union {
		const struct flash_area *area;
	};
	uint16_t block_size;
	uint8_t erase_val;
};

struct data_logger_backend_config {
	const struct data_logger_backend_api *api;
	struct data_logger_backend_data *data;
	union {
		const struct device *backend;
		uint8_t flash_area_id;
	};
	uint32_t logical_blocks;
	uint32_t physical_blocks;
	uint16_t erase_size;
	uint16_t max_block_size;
};

struct data_logger_backend_api {
	/**
	 * @brief Initialise the given backend
	 *
	 * @param backend Backend device to initialise
	 *
	 * @retval 0 on success
	 * @retval -errno otherwise
	 */
	int (*init)(const struct data_logger_backend_config *config);

	/**
	 * @brief Write data to the next backend block
	 *
	 * @param backend Backend device
	 * @param phy_block Physical block index to write data to
	 * @param data_type Data type of the block
	 * @param data Data to write to block
	 * @param data_len Length of data to write
	 *
	 * @retval 0 on success
	 * @retval -errno otherwise
	 */
	int (*write)(const struct data_logger_backend_config *config, uint32_t phy_block,
		     enum infuse_type data_type, const void *data, uint16_t data_len);

	/**
	 * @brief Read data from the given backend block
	 *
	 * @note Reads can run across block boundaries
	 *
	 * @param backend Backend device
	 * @param phy_block Physical block index to read data from
	 * @param block_offset Offset within the block to read from
	 * @param data Data buffer to read into
	 * @param data_len Number of bytes to read
	 *
	 * @retval 0 on success
	 * @retval -errno otherwise
	 */
	int (*read)(const struct data_logger_backend_config *config, uint32_t phy_block,
		    uint16_t block_offset, void *data, uint16_t data_len);

	/**
	 * @brief Erase all data from the given backend
	 *
	 * @param backend Backend device to erase
	 * @param phy_block Physical block index to start erase at
	 * @param num Number of blocks to erase
	 *
	 * @retval 0 on success
	 * @retval -errno otherwise
	 */
	int (*erase)(const struct data_logger_backend_config *config, uint32_t phy_block,
		     uint32_t num);
};

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_BACKEND_H_ */
