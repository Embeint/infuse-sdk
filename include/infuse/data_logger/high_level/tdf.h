/**
 * @file
 * @brief TDF Data Logger
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 *
 * @details
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_HIGH_LEVEL_TDF_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_HIGH_LEVEL_TDF_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief TDF data logger API
 * @defgroup tdf_data_logger_apis TDF data logger APIs
 * @{
 */

/**
 * @brief Add multiple TDFs to a data logger
 *
 * @param dev Data logger
 * @param tdf_id TDF sensor ID
 * @param tdf_len Length of a single TDF
 * @param tdf_num Number of TDFs to add
 * @param time Civil time associated with the first TDF. 0 for no timestamp.
 * @param period Time period between the TDF samples
 * @param data TDF data array
 *
 * @retval 0 On success
 * @retval -errno Error code from @a tdf_add or @a tdf_data_logger_flush on error
 */
int tdf_data_logger_log_array_dev(const struct device *dev, uint16_t tdf_id, uint8_t tdf_len, uint8_t tdf_num,
				  uint64_t time, uint16_t period, void *data);

/**
 * @brief Add a single TDF to a data logger
 *
 * @param dev Data logger
 * @param tdf_id TDF sensor ID
 * @param tdf_len Length of a single TDF
 * @param time Civil time associated with the TDF. 0 for no timestamp.
 * @param data TDF data
 *
 * @retval 0 On success
 * @retval -errno Error code from @a tdf_add or @a tdf_data_logger_flush on error
 */
static inline int tdf_data_logger_log_dev(const struct device *dev, uint16_t tdf_id, uint8_t tdf_len, uint64_t time,
					  void *data)
{
	return tdf_data_logger_log_array_dev(dev, tdf_id, tdf_len, 1, time, 0, data);
}

/**
 * @brief Flush any pending TDFs to backend
 *
 * @param dev Data logger
 *
 * @retval 0 On success (Or no data to flush)
 * @retval -errno Error code from @a data_logger_block_write
 */
int tdf_data_logger_flush_dev(const struct device *dev);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_HIGH_LEVEL_TDF_H_ */
