/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_BACKEND_SHIM_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_BACKEND_SHIM_H_

#include <stdint.h>

#include <zephyr/kernel.h>

#include <infuse/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Data Logger shim backend API
 * @defgroup data_logger_backend_shim Data Logger shim backend
 * @ingroup infuse_data_logger
 * @{
 */

/** Data structure used for shim */
struct data_logger_shim_function_data {
	struct {
		/** Number of times the write function has been called */
		uint32_t num_calls;
		/** Physical block of the last write */
		uint32_t block;
		/** Length of the last write */
		uint16_t data_len;
		/** Data type of the last write */
		enum infuse_type data_type;
		/** Value to return from the write function */
		int rc;
	} write; /**< Write function state */
	struct {
		/** Number of times the read function has been called */
		uint32_t num_calls;
		/** Physical block of the last read */
		uint32_t block;
		/** Block offset of the last read */
		uint16_t block_offset;
		/** Length of the last read */
		uint16_t data_len;
		/** Optional callback run on each read */
		void (*read_cb)(uint32_t block, uint16_t block_offset, uint16_t data_len);
		/** Value to return from the read function */
		int rc;
	} read; /**< Read function state */
	struct {
		/** Number of times the erase function has been called */
		uint32_t num_calls;
		/** First physical block of the last erase */
		uint32_t phy_block;
		/** Number of blocks in the last erase */
		uint32_t num;
		/** Optional semaphore the erase function waits on before returning */
		struct k_sem *block_until;
		/** Value to return from the erase function */
		int rc;
	} erase; /**< Erase function state */
	struct {
		/** Number of times the reset function has been called */
		uint32_t num_calls;
		/** Block hint of the last reset */
		uint32_t block_hint;
		/** Optional semaphore the reset function waits on before returning */
		struct k_sem *block_until;
		/** Value to return from the reset function */
		int rc;
	} reset; /**< Reset function state */
};

/**
 * @brief Re-initialise the logger
 *
 * @param dev Device to re-initialise
 *
 * @retval 0 on success
 * @retval -errno on error
 */
int logger_shim_init(const struct device *dev);

/**
 * @brief Update the current data size of the shim backend
 *
 * @param dev Device to update
 * @param block_size New block size
 */
void logger_shim_change_size(const struct device *dev, uint16_t block_size);

/**
 * @brief Set the current block of the shim backend
 *
 * @param dev Device to update
 * @param current_block New value for the current block
 */
void logger_shim_set_current_block(const struct device *dev, uint32_t current_block);

/**
 * @brief Get the pointer to the function data struct
 *
 * @param dev Device to get pointer from
 *
 * @return Pointer to the function data struct
 */
struct data_logger_shim_function_data *
data_logger_backend_shim_data_pointer(const struct device *dev);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_BACKEND_SHIM_H_ */
