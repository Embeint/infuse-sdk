/**
 * @file
 * @brief Infuse-IoT zbus channels
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_ZBUS_CHANNELS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_ZBUS_CHANNELS_H_

#include <zephyr/zbus/zbus.h>

#include <infuse/zbus/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Infuse zbus channels API
 * @defgroup infuse_zbus_channels_apis Infuse zbus channels APIs
 * @{
 */

/**
 * @brief Define an Infuse zbus channel with default parameters
 *
 * @param channel Infuse channel identifier (@ref infuse_zbus_channel_id)
 */
#define INFUSE_ZBUS_CHAN_DEFINE(channel)                                                           \
	ZBUS_CHAN_DEFINE_WITH_ID(INFUSE_ZBUS_NAME(channel), channel, INFUSE_ZBUS_TYPE(channel),    \
				 NULL, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

/** @cond INTERNAL_HIDDEN */
#define _INFUSE_ZBUS_CHAN_EXTERN(channel) _ZBUS_CHAN_EXTERN(INFUSE_ZBUS_NAME(channel))
/** @endcond */

/**
 * @brief Like ZBUS_CHAN_DECLARE for Infuse channels
 */
#define INFUSE_ZBUS_CHAN_DECLARE(...) FOR_EACH(_INFUSE_ZBUS_CHAN_EXTERN, (;), __VA_ARGS__)

/**
 * @brief Statically get pointer to Infuse channel
 *
 * @param channel Infuse channel identifier (@ref infuse_zbus_channel_id)
 */
#define INFUSE_ZBUS_CHAN_GET(channel) (&INFUSE_ZBUS_NAME(channel))

/**
 * @brief Retrieve the age of the data in the zbus channel
 *
 * @param chan Channel to query
 *
 * @retval UINT64_MAX if channel has never been published to
 * @retval age_ms Data age in milliseconds otherwise
 */
static inline uint64_t infuse_zbus_channel_data_age(const struct zbus_channel *chan)
{
#ifdef CONFIG_ZBUS_CHANNEL_PUBLISH_STATS
	if (zbus_chan_pub_stats_count(chan) == 0) {
		return UINT64_MAX;
	}
	return k_ticks_to_ms_floor64(k_uptime_ticks() - zbus_chan_pub_stats_last_time(chan));
#else
	return UINT64_MAX;
#endif /* CONFIG_ZBUS_CHANNEL_PUBLISH_STATS */
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_ZBUS_CHANNELS_H_ */
