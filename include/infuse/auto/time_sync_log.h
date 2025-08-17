/**
 * @file
 * @brief Automatically log time sync events
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_AUTO_TIME_SYNC_LOG_H_
#define INFUSE_SDK_INCLUDE_INFUSE_AUTO_TIME_SYNC_LOG_H_

#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup time_sync_log_apis Auto: Time Sync Log
 * @{
 */

enum {
	/* Automatically log time sync events */
	AUTO_TIME_SYNC_LOG_SYNCS = BIT(0),
	/* Automatically log reboot information if the application goes from no time source
	 * knowledge to some time source knowledge. This can be used to get an accurate time of the
	 * reboot even if the time knowledge is delayed.
	 */
	AUTO_TIME_SYNC_LOG_REBOOT_ON_SYNC = BIT(1),
};

/**
 * @brief Automatically log time sync events
 *
 * @param tdf_logger_mask TDF data logger mask to log events to
 * @param flags Extra `AUTO_TIME_SYNC_LOG_` flags
 */
void auto_time_sync_log_configure(uint8_t tdf_logger_mask, uint8_t flags);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_AUTO_TIME_SYNC_LOG_H_ */
