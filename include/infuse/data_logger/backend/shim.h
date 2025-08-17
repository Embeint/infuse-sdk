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
 * @{
 */

/** Data structure used for shim */
struct data_logger_shim_function_data {
	struct {
		uint32_t num_calls;
		uint32_t block;
		uint16_t data_len;
		enum infuse_type data_type;
		int rc;
	} write;
	struct {
		uint32_t num_calls;
		uint32_t block;
		uint16_t block_offset;
		uint16_t data_len;
		int rc;
	} read;
	struct {
		uint32_t num_calls;
		uint32_t phy_block;
		uint32_t num;
		struct k_sem *block_until;
		int rc;
	} erase;
	struct {
		uint32_t num_calls;
		uint32_t block_hint;
		struct k_sem *block_until;
		int rc;
	} reset;
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
