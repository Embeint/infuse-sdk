/**
 * @file
 * @brief Infuse-IoT zbus channels
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_ZBUS_CHANNELS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_ZBUS_CHANNELS_H_

#include <stdint.h>

#include <zephyr/zbus/zbus.h>

#include <infuse/tdf/definitions.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Infuse zbus channels API
 * @defgroup infuse_zbus_channels_apis Infuse zbus channels APIs
 * @{
 */

enum infuse_zbus_channel_id {
	INFUSE_ZBUS_CHAN_BASE = 0x43210000,
	/** @brief Data type: @ref tdf_battery_state */
	INFUSE_ZBUS_CHAN_BATTERY = INFUSE_ZBUS_CHAN_BASE + 0,
	/** @brief Data type: @ref tdf_ambient_temp_pres_hum */
	INFUSE_ZBUS_CHAN_AMBIENT_ENV = INFUSE_ZBUS_CHAN_BASE + 1,
	/** @brief Data type: @ref imu_sample_array */
	INFUSE_ZBUS_CHAN_IMU = INFUSE_ZBUS_CHAN_BASE + 2,
	/** @brief Data type: @ref tdf_gcs_wgs84_llha */
	INFUSE_ZBUS_CHAN_LOCATION = INFUSE_ZBUS_CHAN_BASE + 3,
};

#define _INFUSE_ZBUS_CHAN_BATTERY_TYPE     struct tdf_battery_state
#define _INFUSE_ZBUS_CHAN_AMBIENT_ENV_TYPE struct tdf_ambient_temp_pres_hum
#define _INFUSE_ZBUS_CHAN_IMU_TYPE         struct imu_sample_array
#define _INFUSE_ZBUS_CHAN_LOCATION_TYPE    struct tdf_gcs_wgs84_llha

#define _INFUSE_ZBUS_CHAN_BATTERY_NAME     zbus_infuse_battery
#define _INFUSE_ZBUS_CHAN_AMBIENT_ENV_NAME zbus_infuse_ambient_env
#define _INFUSE_ZBUS_CHAN_IMU_NAME         zbus_infuse_imu
#define _INFUSE_ZBUS_CHAN_LOCATION_NAME    zbus_infuse_location

/** @brief Get the type associated with an Infuse zbus channel */
#define INFUSE_ZBUS_TYPE(channel) _##channel##_TYPE
/** @brief Get the channel name associated with an Infuse zbus channel */
#define INFUSE_ZBUS_NAME(channel) _##channel##_NAME

/**
 * @brief Define an Infuse zbus channel with default parameters
 *
 * @param channel Infuse channel identifier (@ref infuse_zbus_channel_id)
 */
#define INFUSE_ZBUS_CHAN_DEFINE(channel)                                                           \
	ZBUS_CHAN_ID_DEFINE(INFUSE_ZBUS_NAME(channel), channel, INFUSE_ZBUS_TYPE(channel), NULL,   \
			    NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

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
	if (zbus_chan_publish_count(chan) == 0) {
		return UINT64_MAX;
	}
	return k_ticks_to_ms_floor64(k_uptime_ticks() - zbus_chan_publish_time(chan));
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
