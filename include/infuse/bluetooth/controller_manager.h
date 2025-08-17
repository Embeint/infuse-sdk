/**
 * @file
 * @brief Manage Bluetooth controller not part of main application image
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_BLUETOOTH_CONTROLLER_MANAGE_H_
#define INFUSE_SDK_INCLUDE_INFUSE_BLUETOOTH_CONTROLLER_MANAGE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief controller_manager API
 * @defgroup controller_manager_apis Bluetooth controller management APIs
 * @{
 */

/**
 * @brief Initialise management of Bluetooth controller
 *
 * @retval 0 on success
 * @retval -errno on error
 */
int bt_controller_manager_init(void);

/**
 * @brief Start Bluetooth controller file write
 *
 * @param ctx Context value that must be provided to future calls
 * @param action Type of file being written
 * @param image_len Length of the file
 *
 * @retval 0 On success
 * @retval -errno On error
 */
int bt_controller_manager_file_write_start(uint32_t *ctx, uint8_t action, size_t image_len);

/**
 * @brief Write the next chunk of the file to the Bluetooth controller
 *
 * @param ctx Context value from @ref bt_controller_manager_file_write_start
 * @param image_offset Byte offset of this chunk
 * @param image_chunk Pointer to the chunk
 * @param chunk_len Length of the chunk
 *
 * @retval 0 On success
 * @retval -errno On error
 */
int bt_controller_manager_file_write_next(uint32_t ctx, uint32_t image_offset,
					  const void *image_chunk, size_t chunk_len);

/**
 * @brief Finish the Bluetooth controller file write
 *
 * @param ctx Context value from @ref bt_controller_manager_file_write_start
 * @param len Output length of data received by the controller
 * @param crc Output CRC of data received by the controller
 *
 * @retval 0 On success
 * @retval -errno On error
 */
int bt_controller_manager_file_write_finish(uint32_t ctx, uint32_t *len, uint32_t *crc);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_BLUETOOTH_CONTROLLER_MANAGE_H_ */
