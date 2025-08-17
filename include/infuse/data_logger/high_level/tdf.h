/**
 * @file
 * @brief TDF Data Logger
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_HIGH_LEVEL_TDF_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_HIGH_LEVEL_TDF_H_

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/sys/util.h>

#include <infuse/tdf/tdf.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief TDF data logger API
 * @defgroup tdf_data_logger_apis TDF data logger APIs
 * @{
 */

/** @cond INTERNAL_HIDDEN */

enum {
	/** DT_NODELABEL(tdf_logger_flash) */
	_TDF_DATA_LOGGER_FLASH_OFFSET = 0,
	/** DT_NODELABEL(tdf_logger_removable) */
	_TDF_DATA_LOGGER_REMOVABLE_OFFSET = 1,
	/** DT_NODELABEL(tdf_logger_serial) */
	_TDF_DATA_LOGGER_SERIAL_OFFSET = 2,
	/** DT_NODELABEL(tdf_logger_udp) */
	_TDF_DATA_LOGGER_UDP_OFFSET = 3,
	/** DT_NODELABEL(tdf_logger_bt_adv) */
	_TDF_DATA_LOGGER_BT_ADV_OFFSET = 4,
	/** DT_NODELABEL(tdf_logger_bt_peripheral) */
	_TDF_DATA_LOGGER_BT_PERIPHERAL_OFFSET = 5,
};

/** @endcond */

/** TDF data logger backends */
enum tdf_data_logger_mask {
	/** Permanent flash storage device */
	TDF_DATA_LOGGER_FLASH = BIT(_TDF_DATA_LOGGER_FLASH_OFFSET),
	/** Removable flash storage device */
	TDF_DATA_LOGGER_REMOVABLE = BIT(_TDF_DATA_LOGGER_REMOVABLE_OFFSET),
	/** Serial communications interface */
	TDF_DATA_LOGGER_SERIAL = BIT(_TDF_DATA_LOGGER_SERIAL_OFFSET),
	/** UDP communications interface */
	TDF_DATA_LOGGER_UDP = BIT(_TDF_DATA_LOGGER_UDP_OFFSET),
	/** Bluetooth advertising communications interface */
	TDF_DATA_LOGGER_BT_ADV = BIT(_TDF_DATA_LOGGER_BT_ADV_OFFSET),
	/** Bluetooth GATT peripheral communications interface */
	TDF_DATA_LOGGER_BT_PERIPHERAL = BIT(_TDF_DATA_LOGGER_BT_PERIPHERAL_OFFSET),
};

/**
 * @brief Add multiple TDFs to a data logger
 *
 * @param dev Data logger
 * @param tdf_id TDF sensor ID
 * @param tdf_len Length of a single TDF
 * @param tdf_num Number of TDFs to add
 * @param format TDF data encoding format
 * @param time Epoch time associated with the first TDF. 0 for no timestamp.
 * @param idx_period Index of first sample if @a format == TDF_DATA_FORMAT_IDX_ARRAY
 *                   Epoch time between tdfs when @a tdf_num > 0 otherwise.
 * @param data TDF data array
 *
 * @retval 0 On success
 * @retval -errno Error code from @a tdf_add or @a tdf_data_logger_flush on error
 */
int tdf_data_logger_log_core_dev(const struct device *dev, uint16_t tdf_id, uint8_t tdf_len,
				 uint8_t tdf_num, enum tdf_data_format format, uint64_t time,
				 uint32_t idx_period, const void *data);

/**
 * @brief Add multiple TDFs to multiple data loggers
 *
 * @param logger_mask Bitmask of loggers to write to (@ref tdf_data_logger_mask)
 * @param tdf_id TDF sensor ID
 * @param tdf_len Length of a single TDF
 * @param tdf_num Number of TDFs to add
 * @param format TDF data encoding format
 * @param time Epoch time associated with the first TDF. 0 for no timestamp.
 * @param idx_period Index of first sample if @a format == TDF_DATA_FORMAT_IDX_ARRAY
 *                   Epoch time between tdfs when @a tdf_num > 0 otherwise.
 * @param data TDF data array
 */
void tdf_data_logger_log_core(uint8_t logger_mask, uint16_t tdf_id, uint8_t tdf_len,
			      uint8_t tdf_num, enum tdf_data_format format, uint64_t time,
			      uint32_t idx_period, const void *data);

/**
 * @brief Add multiple TDFs to a data logger
 *
 * @param dev Data logger
 * @param tdf_id TDF sensor ID
 * @param tdf_len Length of a single TDF
 * @param tdf_num Number of TDFs to add
 * @param time Epoch time associated with the first TDF. 0 for no timestamp.
 * @param period Time period between the TDF samples
 * @param data TDF data array
 *
 * @retval 0 On success
 * @retval -errno Error code from @a tdf_add or @a tdf_data_logger_flush on error
 */
static inline int tdf_data_logger_log_array_dev(const struct device *dev, uint16_t tdf_id,
						uint8_t tdf_len, uint8_t tdf_num, uint64_t time,
						uint32_t period, const void *data)
{
	return tdf_data_logger_log_core_dev(dev, tdf_id, tdf_len, tdf_num,
					    TDF_DATA_FORMAT_TIME_ARRAY, time, period, data);
}

/**
 * @brief Add multiple TDFs to multiple data loggers
 *
 * @param logger_mask Bitmask of loggers to write to (@ref tdf_data_logger_mask)
 * @param tdf_id TDF sensor ID
 * @param tdf_len Length of a single TDF
 * @param tdf_num Number of TDFs to add
 * @param time Epoch time associated with the first TDF. 0 for no timestamp.
 * @param period Time period between the TDF samples
 * @param data TDF data array
 */
static inline void tdf_data_logger_log_array(uint8_t logger_mask, uint16_t tdf_id, uint8_t tdf_len,
					     uint8_t tdf_num, uint64_t time, uint32_t period,
					     const void *data)
{
	tdf_data_logger_log_core(logger_mask, tdf_id, tdf_len, tdf_num, TDF_DATA_FORMAT_TIME_ARRAY,
				 time, period, data);
}

/**
 * @brief Add a single TDF to a data logger
 *
 * @param dev Data logger
 * @param tdf_id TDF sensor ID
 * @param tdf_len Length of a single TDF
 * @param time Epoch time associated with the TDF. 0 for no timestamp.
 * @param data TDF data
 *
 * @retval 0 On success
 * @retval -errno Error code from @a tdf_add or @a tdf_data_logger_flush on error
 */
static inline int tdf_data_logger_log_dev(const struct device *dev, uint16_t tdf_id,
					  uint8_t tdf_len, uint64_t time, const void *data)
{
	return tdf_data_logger_log_core_dev(dev, tdf_id, tdf_len, 1, TDF_DATA_FORMAT_SINGLE, time,
					    0, data);
}

/**
 * @brief Add a single TDF to multiple data loggers
 *
 * @param logger_mask Bitmask of loggers to write to (@ref tdf_data_logger_mask)
 * @param tdf_id TDF sensor ID
 * @param tdf_len Length of a single TDF
 * @param time Epoch time associated with the TDF. 0 for no timestamp.
 * @param data TDF data
 */
static inline void tdf_data_logger_log(uint8_t logger_mask, uint16_t tdf_id, uint8_t tdf_len,
				       uint64_t time, const void *data)
{
	tdf_data_logger_log_core(logger_mask, tdf_id, tdf_len, 1, TDF_DATA_FORMAT_SINGLE, time, 0,
				 data);
}

/**
 * @brief Type safe wrapper around @ref tdf_data_logger_log
 *
 * Adds compile-time validation that the passed pointer matches the type associated
 * with @a tdf_id.
 *
 * @note Only works for TDF types without trailing variable length arrays
 *
 * @param logger_mask Bitmask of loggers to write to (@ref tdf_data_logger_mask)
 * @param tdf_id TDF sensor ID
 * @param tdf_time Epoch time associated with the TDF. 0 for no timestamp.
 * @param data TDF data
 */
#define TDF_DATA_LOGGER_LOG(logger_mask, tdf_id, tdf_time, data)                                   \
	tdf_data_logger_log(logger_mask, tdf_id, sizeof(TDF_TYPE(tdf_id)), tdf_time, data);        \
	do {                                                                                       \
		__maybe_unused const TDF_TYPE(tdf_id) *_data = data;                               \
	} while (0)

/**
 * @brief Type safe wrapper around @ref tdf_data_logger_log_array
 *
 * Adds compile-time validation that the passed pointer matches the type associated
 * with @a tdf_id.
 *
 * @note Only works for TDF types without trailing variable length arrays
 *
 * @param logger_mask Bitmask of loggers to write to (@ref tdf_data_logger_mask)
 * @param tdf_id TDF sensor ID
 * @param tdf_num Number of TDFs to add
 * @param tdf_time Epoch time associated with the TDF. 0 for no timestamp.
 * @param period Time period between the TDF samples
 * @param data TDF data
 */
#define TDF_DATA_LOGGER_LOG_ARRAY(logger_mask, tdf_id, tdf_num, tdf_time, period, data)            \
	tdf_data_logger_log_array(logger_mask, tdf_id, sizeof(TDF_TYPE(tdf_id)), tdf_num,          \
				  tdf_time, period, data);                                         \
	do {                                                                                       \
		__maybe_unused const TDF_TYPE(tdf_id) *_data = data;                               \
	} while (0)

/**
 * @brief Query the number of bytes pending on the current block
 *
 * @param dev Data logger
 *
 * @return int Number of bytes of TDF data currently pending on the block
 */
int tdf_data_logger_block_bytes_pending(const struct device *dev);

/**
 * @brief Query the number of bytes remaining until the logger block will be flushed
 *
 * @param dev Data logger
 *
 * @return int Number of bytes of TDF data that can be added on the current block
 */
int tdf_data_logger_block_bytes_remaining(const struct device *dev);

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
 * @param logger_mask Bitmask of loggers to flush (@ref tdf_data_logger_mask)
 */
void tdf_data_logger_flush(uint8_t logger_mask);

/**
 * @brief Set the remote ID associated with the logger
 *
 * If the logger was previously logging a different remote ID, any pending data
 * will be flushed.
 *
 * @param dev TDF logger to update
 * @param remote_id ID of the remote device
 *
 * @retval 0 on success
 * @retval -EINVAL Logger is not a remote TDF logger
 */
int tdf_data_logger_remote_id_set(const struct device *dev, uint64_t remote_id);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_HIGH_LEVEL_TDF_H_ */
