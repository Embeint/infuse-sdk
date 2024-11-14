/**
 * @file
 * @brief Core data logger abstraction
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
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
	/* Number of logical blocks on the logger */
	uint32_t logical_blocks;
	/* Number of physical blocks on the logger */
	uint32_t physical_blocks;
	/* Number of logical blocks that have been written */
	uint32_t current_block;
	/* Earliest logical block that still exists on the logger */
	uint32_t earliest_block;
	/* Size of a single block in bytes */
	uint16_t block_size;
	/* Number of bytes at the start of the block that should not contain data */
	uint16_t block_overhead;
	/* Minimum erase unit of the logger in bytes */
	uint16_t erase_unit;
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
		DT_NODE_HAS_COMPAT(node_id, embeint_data_logger_flash_map), (512),                 \
		(COND_CODE_1(DT_NODE_HAS_COMPAT(node_id, embeint_data_logger_epacket),             \
			     (EPACKET_INTERFACE_MAX_PAYLOAD(DT_PROP(node_id, epacket))),           \
			     ((COND_CODE_1(DT_NODE_HAS_COMPAT(node_id, embeint_data_logger_exfat), \
					   (512), ()))))))

/**
 * @brief Get the current data logger state
 *
 * @param dev Data logger to query
 * @param state State storage
 *
 * @retval 0 on success
 */
void data_logger_get_state(const struct device *dev, struct data_logger_state *state);

/**
 * @brief Write a block to the data logger
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
 * @retval -errno on error
 */
int data_logger_block_read(const struct device *dev, uint32_t block_idx, uint16_t block_offset,
			   void *block, uint16_t block_len);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_LOGGER_H_ */
