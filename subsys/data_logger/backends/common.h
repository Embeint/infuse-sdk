/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_SUBSYS_DATA_LOGGER_COMMON_H_
#define INFUSE_SDK_SUBSYS_DATA_LOGGER_COMMON_H_

#include <stdint.h>

#include <zephyr/sys/slist.h>
#include <zephyr/toolchain.h>

#include <infuse/types.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	/** Data logger is currently being erased */
	DATA_LOGGER_FLAGS_ERASING = BIT(0),
};

/** Must be first member of data struct */
struct data_logger_common_data {
	sys_slist_t callbacks;
	uint64_t bytes_logged;
	uint32_t logical_blocks;
	uint32_t physical_blocks;
	uint32_t boot_block;
	uint32_t current_block;
	uint32_t earliest_block;
	uint32_t erase_size;
	uint16_t block_size;
	uint8_t erase_val;
	uint8_t flags;
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
	/* Block writes must be aligned to this length */
	uint8_t block_write_align;
	/* Writes must contain the complete block size */
	bool requires_full_block_write;
	/* Write function only queues writes, does not wait for completion */
	bool queued_writes;
};

#define COMMON_CONFIG_PRE(inst)                                                                    \
	IF_ENABLED(CONFIG_DATA_LOGGER_RAM_BUFFER,                                                  \
		   (static uint8_t ram_buf_##inst[DT_INST_PROP(inst, extra_ram_buffer)]            \
		    __aligned(4)))

#define COMMON_CONFIG_INIT(inst, _full_block_write, _queued_writes, _block_write_align)            \
	COND_CODE_1(CONFIG_DATA_LOGGER_RAM_BUFFER,                                                 \
		    ({                                                                             \
			    .ram_buf_data = ram_buf_##inst,                                        \
			    .ram_buf_len = sizeof(ram_buf_##inst),                                 \
			    .block_write_align = _block_write_align,                               \
			    .requires_full_block_write = _full_block_write,                        \
			    .queued_writes = _queued_writes,                                       \
		    }),                                                                            \
		    ({                                                                             \
			    .block_write_align = _block_write_align,                               \
			    .requires_full_block_write = _full_block_write,                        \
			    .queued_writes = _queued_writes,                                       \
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

#if defined(CONFIG_DATA_LOGGER_BURST_WRITES) || defined(__DOXYGEN__)
	/**
	 * @brief Write multiple blocks to the logger at once
	 *
	 * This function enables taking advantage of multiple blocks sitting in a contiguous
	 * RAM buffer to reduce transaction overhead and therefore increase write throughput.
	 *
	 * @param dev Logger device
	 * @param start_block Physical block index to start data write at
	 * @param num_blocks Number of blocks of data
	 * @param data Data for @a num_blocks blocks to write
	 *
	 * @retval 0 on success
	 * @retval -errno otherwise
	 */
	int (*write_burst)(const struct device *dev, uint32_t start_block, uint32_t num_blocks,
			   const void *data);
#endif /* CONFIG_DATA_LOGGER_BURST_WRITES */

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
	 * @brief Erase data from the logger
	 *
	 * @param dev Logger device
	 * @param phy_block Physical block index to start erase at
	 * @param num Number of blocks to erase
	 *
	 * @retval 0 on success
	 * @retval -errno otherwise
	 */
	int (*erase)(const struct device *dev, uint32_t phy_block, uint32_t num);

	/**
	 * @brief Reset logger back to empty state
	 *
	 * @param dev Logger device
	 * @param block_hint Hint of how many blocks of data are currently logged
	 * @param erase_progress Callback to periodically run with erase progress
	 *
	 * @retval 0 on success
	 * @retval -errno otherwise
	 */
	int (*reset)(const struct device *dev, uint32_t block_hint,
		     void (*erase_progress)(uint32_t blocks_erased));

	/**
	 * @brief Search range hint for initialisation
	 *
	 * Optional method to inform upper layer about the block range at which the
	 * last block can be found. This can be used to optimize the search process
	 * for loggers with high overheads on arbitrary reads.
	 *
	 * @param dev Logger device
	 * @param hint_start First block that might be the last block with valid data
	 * @param hint_end Last block that might be the last block with valid data
	 *
	 * @retval 0 on success
	 * @retval -errno otherwise
	 */
	int (*search_hint)(const struct device *dev, uint32_t *hint_start, uint32_t *hint_end);
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

/**
 * @brief Handle the block size of a logger changing at runtime.
 *
 * This is only expected to occur for networked backends, which can connect
 * with different MTU's at runtime.
 *
 * @param dev Data logger instance
 * @param block_size Updated block size
 */
void data_logger_common_block_size_changed(const struct device *dev, uint16_t block_size);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_SUBSYS_DATA_LOGGER_COMMON_H_ */
