/**
 * @file
 * @brief Core data logger abstraction
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_LOGGER_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_LOGGER_H_

#include <stdint.h>

#include <infuse/types.h>
#include <infuse/epacket/interface.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Data logger API
 * @defgroup data_logger_apis data logger APIs
 * @{
 */

struct data_logger_state {
	/* Bytes logged since reboot */
	uint64_t bytes_logged;
	/* Number of logical blocks on the logger */
	uint32_t logical_blocks;
	/* Number of physical blocks on the logger */
	uint32_t physical_blocks;
	/* Number of blocks present at boot */
	uint32_t boot_block;
	/* Number of logical blocks that have been written */
	uint32_t current_block;
	/* Earliest logical block that still exists on the logger */
	uint32_t earliest_block;
	/* Minimum erase unit of the logger in bytes */
	uint32_t erase_unit;
	/* Size of a single block in bytes */
	uint16_t block_size;
	/* Number of bytes at the start of the block that should not contain data */
	uint16_t block_overhead;
	/* Writes require the full block size to be provided */
	bool requires_full_block_write;
};

/* Header on every block logged to persistent storage */
struct data_logger_persistent_block_header {
	/* One byte wrap count (1 - 254) */
	uint8_t block_wrap;
	/* Type of block data */
	uint8_t block_type;
} __packed;

struct data_logger_cb {
	/**
	 * @brief Data logger has changed the maximum block size
	 *
	 * @param dev Data logger that changed
	 * @param block_size New maximum block size
	 * @param user_data User context from callback structure
	 */
	void (*block_size_update)(const struct device *dev, uint16_t block_size, void *user_data);
	/**
	 * @brief Writing to the data logger has succeeded
	 *
	 * @param dev Data logger that was written to
	 * @param data_type Data type of the data logger block
	 * @param user_data User context from callback structure
	 */
	void (*write_success)(const struct device *dev, enum infuse_type data_type,
			      void *user_data);
	/**
	 * @brief Writing to the data logger has failed
	 *
	 * @param dev Data logger that failed to write
	 * @param data_type Data type of the data logger block
	 * @param mem Pointer to the block data
	 * @param mem_len Length of the block data
	 * @param reason Failure reason
	 * @param user_data User context from callback structure
	 */
	void (*write_failure)(const struct device *dev, enum infuse_type data_type, const void *mem,
			      uint16_t mem_len, int reason, void *user_data);
	/* Arbitrary user data pointer */
	void *user_data;
	/* Private list iteration field */
	sys_snode_t node;
};

/**
 * @brief Are the dependencies for this data logger met?
 *
 * @param node_id `embeint,data-logger` node ID
 *
 * @retval 1 if data logger dependencies exist in build
 * @retval 0 if data logger dependencies do NOT exist in build
 */
#define DATA_LOGGER_DEPENDENCIES_MET(node_id)                                                      \
	COND_CODE_1(DT_NODE_HAS_COMPAT(node_id, embeint_data_logger_epacket),                      \
		    (EPACKET_INTERFACE_IS_COMPILED_IN(DT_PROP(node_id, epacket))), (1))

/**
 * @brief Maximum required block size for each logger backend
 *
 * @param node_id `embeint,data-logger` node ID
 *
 * @returns Maximum size of a block on the logger
 */
#define DATA_LOGGER_MAX_SIZE(node_id)                                                              \
	COND_CODE_1(                                                                               \
		DT_NODE_HAS_COMPAT(node_id, embeint_data_logger_flash_map),                        \
		(DT_PROP(node_id, block_size)),                                                    \
		(COND_CODE_1(DT_NODE_HAS_COMPAT(node_id, embeint_data_logger_epacket),             \
			     (EPACKET_INTERFACE_MAX_PAYLOAD(DT_PROP(node_id, epacket))),           \
			     ((COND_CODE_1(DT_NODE_HAS_COMPAT(node_id, embeint_data_logger_exfat), \
					   (512), ()))))))

/**
 * @brief Get the current data logger state
 *
 * @param dev Data logger to query
 * @param state State storage
 */
void data_logger_get_state(const struct device *dev, struct data_logger_state *state);

/**
 * @brief Write a block to the data logger
 *
 * @note Some hardware-accelerated backends may have alignment or size restrictions
 *       for the provided block buffer.
 *
 * @param dev Data logger to write to
 * @param type Data type being written
 * @param block Block data pointer (Overhead bytes will be overwritten)
 * @param block_len Total block data length (Including overhead bytes)
 *
 * @retval 0 on success
 * @retval -ENOTCONN data logger is currently disconnected
 * @retval -EINVAL invalid block length
 * @retval -ENOMEM data logger is full
 * @retval -errno on error
 */
int data_logger_block_write(const struct device *dev, enum infuse_type type, void *block,
			    uint16_t block_len);

/**
 * @brief Read a block from the data logger
 *
 * @note Can read over block boundaries
 *
 * @note First bytes on each block will be the logging overhead
 *
 * @param dev Data logger to read from
 * @param block_idx Logical block number
 * @param block_offset Byte offset within the block
 * @param block Block data pointer
 * @param block_len Block bytes to read
 *
 * @retval 0 on success
 * @retval -ENOTSUP reading not supported by logger
 * @retval -ENOENT requested data that does not exist
 * @retval -EBUSY data logger is being erased
 * @retval -errno on error
 */
int data_logger_block_read(const struct device *dev, uint32_t block_idx, uint16_t block_offset,
			   void *block, uint16_t block_len);

/**
 * @brief Completely erase a data logger
 *
 * Attempting to write to a logger while the erase is ongoing will silently fail with return code 0.
 *
 * @param dev Data logger to erase
 * @param erase_all Run erase function on ALL blocks, not just those with data
 * @param erase_progress Callback periodically run with the erase progress
 *
 * @retval 0 on success
 * @retval -ENOTSUP erasing not supported by logger
 * @retval -errno on error
 */
int data_logger_erase(const struct device *dev, bool erase_all,
		      void (*erase_progress)(uint32_t blocks_erased));

/**
 * @brief Flush any data pending in a RAM buffer to the backend
 *
 * This function only performs useful work on data loggers with an attached
 * RAM buffer.
 *
 * @param dev Data logger to flush
 *
 * @retval 0 on success
 * @retval -ENOTCONN data logger is currently disconnected
 * @retval -ENOMEM data logger is full
 * @retval -errno on error
 */
int data_logger_flush(const struct device *dev);

/**
 * @brief Register for event callbacks from the data logger
 *
 * @param dev Data logger instance
 * @param cb Callback structure
 */
void data_logger_register_cb(const struct device *dev, struct data_logger_cb *cb);

#ifdef CONFIG_ZTEST

/**
 * @brief Function to set internal state for testing purposes
 *
 * @param dev Data logger to set
 * @param enabled True to enable state, false to clear
 */
void data_logger_set_erase_state(const struct device *dev, bool enabled);

#endif /* CONFIG_ZTEST */

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_LOGGER_H_ */
