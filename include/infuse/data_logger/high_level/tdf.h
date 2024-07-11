/**
 * @file
 * @brief TDF Data Logger
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 *
 * @details
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_HIGH_LEVEL_TDF_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_HIGH_LEVEL_TDF_H_

#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief TDF data logger API
 * @defgroup tdf_data_logger_apis TDF data logger APIs
 * @{
 */

enum {
	_TDF_DATA_LOGGER_FLASH_OFFSET = 0,
	_TDF_DATA_LOGGER_REMOVABLE_OFFSET = 1,
	_TDF_DATA_LOGGER_SERIAL_OFFSET = 2,
	_TDF_DATA_LOGGER_UDP_OFFSET = 3,
	_TDF_DATA_LOGGER_BT_ADV_OFFSET = 4,
};

enum {
	/* DT_NODELABEL(tdf_logger_flash) */
	TDF_DATA_LOGGER_FLASH = BIT(_TDF_DATA_LOGGER_FLASH_OFFSET),
	/* DT_NODELABEL(tdf_logger_removable) */
	TDF_DATA_LOGGER_REMOVABLE = BIT(_TDF_DATA_LOGGER_REMOVABLE_OFFSET),
	/* DT_NODELABEL(tdf_logger_serial) */
	TDF_DATA_LOGGER_SERIAL = BIT(_TDF_DATA_LOGGER_SERIAL_OFFSET),
	/* DT_NODELABEL(tdf_logger_udp) */
	TDF_DATA_LOGGER_UDP = BIT(_TDF_DATA_LOGGER_UDP_OFFSET),
	/* DT_NODELABEL(tdf_logger_bt_adv) */
	TDF_DATA_LOGGER_BT_ADV = BIT(_TDF_DATA_LOGGER_BT_ADV_OFFSET),
};

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
int tdf_data_logger_log_array_dev(const struct device *dev, uint16_t tdf_id, uint8_t tdf_len,
				  uint8_t tdf_num, uint64_t time, uint16_t period, void *data);

/**
 * @brief Add multiple TDFs to multiple data loggers
 *
 * @param logger_mask Bitmask of loggers to write to
 * @param tdf_id TDF sensor ID
 * @param tdf_len Length of a single TDF
 * @param tdf_num Number of TDFs to add
 * @param time Civil time associated with the first TDF. 0 for no timestamp.
 * @param period Time period between the TDF samples
 * @param data TDF data array
 */
void tdf_data_logger_log_array(uint8_t logger_mask, uint16_t tdf_id, uint8_t tdf_len,
			       uint8_t tdf_num, uint64_t time, uint16_t period, void *data);

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
static inline int tdf_data_logger_log_dev(const struct device *dev, uint16_t tdf_id,
					  uint8_t tdf_len, uint64_t time, void *data)
{
	return tdf_data_logger_log_array_dev(dev, tdf_id, tdf_len, 1, time, 0, data);
}

/**
 * @brief Add a single TDF to multiple data loggers
 *
 * @param logger_mask Bitmask of loggers to write to
 * @param tdf_id TDF sensor ID
 * @param tdf_len Length of a single TDF
 * @param time Civil time associated with the TDF. 0 for no timestamp.
 * @param data TDF data
 */
static inline void tdf_data_logger_log(uint8_t logger_mask, uint16_t tdf_id, uint8_t tdf_len,
				       uint64_t time, void *data)
{
	tdf_data_logger_log_array(logger_mask, tdf_id, tdf_len, 1, time, 0, data);
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
 * @brief Flush any pending TDFs to multiple backends
 *
 * @param logger_mask Bitmask of loggers to flush
 *
 * @retval 0 On success (Or no data to flush)
 * @retval -errno Error code from @a tdf_data_logger_flush_dev
 */
void tdf_data_logger_flush(uint8_t logger_mask);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_HIGH_LEVEL_TDF_H_ */
