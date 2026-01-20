/**
 * @file
 * @brief Automatic timezones from location
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_AUTO_LOCATION_TIMEZONE_H_
#define INFUSE_SDK_INCLUDE_INFUSE_AUTO_LOCATION_TIMEZONE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief location_timezone API
 * @defgroup location_timezone_apis location_timezone APIs
 * @{
 */

/**
 * @brief Get the current approximate UTC timezone
 *
 * @param timezone Output UTC timezone in hours
 *
 * @retval 0 Local time queried successfully
 * @retval -EAGAIN Local timezone is not known
 */
int location_timezone(int8_t *timezone);

/**
 * @brief Get the current approximate local time
 *
 * The local time is defined such that (local_time_seconds % SEC_PER_DAY) == 0
 * at a time approximately equal to midnight. How far away from midnight this
 * actually occurs depends on the difference between the output of
 * @ref location_timezone and the true timezone.
 *
 * @param local_time_seconds Output local time
 *
 * @retval 0 Local time queried successfully
 * @retval -EAGAIN Local timezone is not known
 */
int location_local_time(uint32_t *local_time_seconds);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_AUTO_LOCATION_TIMEZONE_H_ */
