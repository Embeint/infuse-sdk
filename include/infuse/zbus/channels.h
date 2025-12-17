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

#include <stdint.h>

#include <zephyr/zbus/zbus.h>

#include <infuse/drivers/imu/data_types.h>
#include <infuse/tdf/definitions.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Infuse zbus channels API
 * @defgroup infuse_zbus_channels_apis Infuse zbus channels APIs
 * @{
 */

/** Data type for @ref INFUSE_ZBUS_CHAN_MOVEMENT_STD_DEV */
struct infuse_zbus_chan_movement_std_dev {
	/** Accelerometer standard deviation */
	struct tdf_acc_magnitude_std_dev data;
	/** Expected number of samples for the window */
	uint32_t expected_samples;
	/** Configured threshold for movement detection (micro-g) */
	uint32_t movement_threshold;
};

/** Data type for @ref INFUSE_ZBUS_CHAN_TILT */
struct infuse_zbus_chan_tilt {
	/** Cosine of the tilt angle */
	float cosine;
};

enum infuse_zbus_channel_id {
	INFUSE_ZBUS_CHAN_BASE = 0x43210000,
	/** @brief Data type: @ref tdf_battery_state */
	INFUSE_ZBUS_CHAN_BATTERY = INFUSE_ZBUS_CHAN_BASE + 0,
	/** @brief Data type: @ref tdf_ambient_temp_pres_hum */
	INFUSE_ZBUS_CHAN_AMBIENT_ENV = INFUSE_ZBUS_CHAN_BASE + 1,
	/** @brief Data type: @ref imu_sample_array */
	INFUSE_ZBUS_CHAN_IMU = INFUSE_ZBUS_CHAN_BASE + 2,
	/** @brief Data type: @ref imu_magnitude_array */
	INFUSE_ZBUS_CHAN_IMU_ACC_MAG = INFUSE_ZBUS_CHAN_BASE + 3,
	/** @brief Data type: @ref tdf_gcs_wgs84_llha */
	INFUSE_ZBUS_CHAN_LOCATION = INFUSE_ZBUS_CHAN_BASE + 4,
	/** @brief Data type: @ref infuse_zbus_chan_movement_std_dev */
	INFUSE_ZBUS_CHAN_MOVEMENT_STD_DEV = INFUSE_ZBUS_CHAN_BASE + 5,
	/** @brief Data type: @ref infuse_zbus_chan_tilt */
	INFUSE_ZBUS_CHAN_TILT = INFUSE_ZBUS_CHAN_BASE + 6,
	/** @brief Data type: @ref tdf_ubx_nav_pvt */
	INFUSE_ZBUS_CHAN_UBX_NAV_PVT = INFUSE_ZBUS_CHAN_BASE + 7,
	/** @brief Data type: @ref tdf_nrf9x_gnss_pvt */
	INFUSE_ZBUS_CHAN_NRF9X_NAV_PVT = INFUSE_ZBUS_CHAN_BASE + 8,
	/** @brief Data type: @ref tdf_soc_temperature */
	INFUSE_ZBUS_CHAN_SOC_TEMPERATURE = INFUSE_ZBUS_CHAN_BASE + 9,
};

#define _INFUSE_ZBUS_CHAN_BATTERY_TYPE          struct tdf_battery_state
#define _INFUSE_ZBUS_CHAN_AMBIENT_ENV_TYPE      struct tdf_ambient_temp_pres_hum
#define _INFUSE_ZBUS_CHAN_IMU_TYPE              struct imu_sample_array
#define _INFUSE_ZBUS_CHAN_IMU_ACC_MAG_TYPE      struct imu_magnitude_array
#define _INFUSE_ZBUS_CHAN_LOCATION_TYPE         struct tdf_gcs_wgs84_llha
#define _INFUSE_ZBUS_CHAN_MOVEMENT_STD_DEV_TYPE struct infuse_zbus_chan_movement_std_dev
#define _INFUSE_ZBUS_CHAN_TILT_TYPE             struct infuse_zbus_chan_tilt
#define _INFUSE_ZBUS_CHAN_UBX_NAV_PVT_TYPE      struct tdf_ubx_nav_pvt
#define _INFUSE_ZBUS_CHAN_NRF9X_NAV_PVT_TYPE    struct tdf_nrf9x_gnss_pvt
#define _INFUSE_ZBUS_CHAN_SOC_TEMPERATURE_TYPE  struct tdf_soc_temperature

#define _INFUSE_ZBUS_CHAN_BATTERY_NAME          zbus_infuse_battery
#define _INFUSE_ZBUS_CHAN_AMBIENT_ENV_NAME      zbus_infuse_ambient_env
#define _INFUSE_ZBUS_CHAN_IMU_NAME              zbus_infuse_imu
#define _INFUSE_ZBUS_CHAN_IMU_ACC_MAG_NAME      zbus_infuse_imu_acc_mag
#define _INFUSE_ZBUS_CHAN_LOCATION_NAME         zbus_infuse_location
#define _INFUSE_ZBUS_CHAN_MOVEMENT_STD_DEV_NAME zbus_infuse_move_std_dev
#define _INFUSE_ZBUS_CHAN_TILT_NAME             zbus_infuse_tilt
#define _INFUSE_ZBUS_CHAN_UBX_NAV_PVT_NAME      zbus_infuse_ubx_nav_pvt
#define _INFUSE_ZBUS_CHAN_NRF9X_NAV_PVT_NAME    zbus_infuse_nrf9x_nav_pvt
#define _INFUSE_ZBUS_CHAN_SOC_TEMPERATURE_NAME  zbus_infuse_soc_temperature

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
