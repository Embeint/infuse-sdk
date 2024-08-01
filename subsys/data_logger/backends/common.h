/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_SUBSYS_DATA_LOGGER_COMMON_H_
#define INFUSE_SDK_SUBSYS_DATA_LOGGER_COMMON_H_

#include <stdint.h>

#include <zephyr/toolchain.h>

#include <infuse/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Must be first member of data struct */
struct data_logger_common_data {
	uint32_t logical_blocks;
	uint32_t physical_blocks;
	uint32_t current_block;
	uint32_t earliest_block;
	uint16_t block_size;
	uint16_t erase_size;
	uint8_t erase_val;
#ifdef CONFIG_DATA_LOGGER_RAM_BUFFER
	size_t ram_buf_offset;
#endif /* CONFIG_DATA_LOGGER_RAM_BUFFER */
};

/** Must be first member of config struct */
struct data_logger_common_config {
#ifdef CONFIG_DATA_LOGGER_RAM_BUFFER
	uint8_t *ram_buf_data;
	size_t ram_buf_len;
#endif /* CONFIG_DATA_LOGGER_RAM_BUFFER */
	bool requires_full_block_write;
};

#define COMMON_CONFIG_PRE(inst)                                                                    \
	IF_ENABLED(CONFIG_DATA_LOGGER_RAM_BUFFER,                                                  \
		   (static uint8_t ram_buf_##inst[DT_INST_PROP(inst, extra_ram_buffer)]))

#define COMMON_CONFIG_INIT(inst, full_block_write)                                                 \
	COND_CODE_1(CONFIG_DATA_LOGGER_RAM_BUFFER,                                                 \
		    ({                                                                             \
			    .ram_buf_data = ram_buf_##inst,                                        \
			    .ram_buf_len = sizeof(ram_buf_##inst),                                 \
			    .requires_full_block_write = full_block_write,                         \
		    }),                                                                            \
		    ({                                                                             \
			    .requires_full_block_write = full_block_write,                         \
		    }))

struct data_logger_api {
	/**
	 * @brief Write data to the logger block
	 *
	 * @param dev Logger device
	 * @param phy_block Physical block index to write data to
	 * @param data_type Data type of the block
	 * @param data Data to write to block
	 * @param data_len Length of data to write
	 *
	 * @retval 0 on success
	 * @retval -errno otherwise
	 */
	int (*write)(const struct device *dev, uint32_t phy_block, enum infuse_type data_type,
		     const void *data, uint16_t data_len);

	/**
	 * @brief Read data from the logger
	 *
	 * @note Reads can run across block boundaries
	 *
	 * @param dev Logger device
	 * @param phy_block Physical block index to read data from
	 * @param block_offset Offset within the block to read from
	 * @param data Data buffer to read into
	 * @param data_len Number of bytes to read
	 *
	 * @retval 0 on success
	 * @retval -errno otherwise
	 */
	int (*read)(const struct device *dev, uint32_t phy_block, uint16_t block_offset, void *data,
		    uint16_t data_len);

	/**
	 * @brief Erase all data from the logger
	 *
	 * @param dev Logger device
	 * @param phy_block Physical block index to start erase at
	 * @param num Number of blocks to erase
	 *
	 * @retval 0 on success
	 * @retval -errno otherwise
	 */
	int (*erase)(const struct device *dev, uint32_t phy_block, uint32_t num);
};

/**
 * @brief Common data logger init
 *
 * @param dev Data logger instance
 *
 * @retval 0 on success
 * @retval -errno error code from API read on error
 */
int data_logger_common_init(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_SUBSYS_DATA_LOGGER_COMMON_H_ */
