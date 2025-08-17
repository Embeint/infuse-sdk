/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * Conversions between UBX and Zephyr defines
 */

#ifndef INFUSE_GNSS_UBX_ZEPHYR_H_
#define INFUSE_GNSS_UBX_ZEPHYR_H_

#include <zephyr/drivers/gnss.h>

#include <infuse/gnss/ubx/cfg.h>
#include <infuse/gnss/ubx/defines.h>
#include <infuse/gnss/ubx/protocol.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup ubx_zephyr_conv UBX Zephyr Conversions
 * @{
 */

/**
 * @brief Convert UBX GNSS ID to Zephyr GNSS ID
 *
 * @param gnss_id UBX GNSS ID
 *
 * @retval -EINVAL On invalid @a gnss_id
 * @retval gnss_system Equivalent Zephyr GNSS ID
 */
int ubx_gnss_id_to_gnss_system(enum ubx_gnss_id gnss_id);

/**
 * @brief Convert Zephyr GNSS ID to UBX GNSS ID
 *
 * @param gnss_system Zephyr GNSS ID
 *
 * @retval -EINVAL On invalid @a gnss_system
 * @retval gnss_id Equivalent UBX GNSS ID
 */
int gnss_system_to_ubx_gnss_id(enum gnss_system gnss_system);

/**
 * @brief Get fix status from NAV-PVT message
 *
 * @param pvt NAV-PVT message
 *
 * @retval status Zephyr fix status
 */
static inline enum gnss_fix_status ubx_nav_pvt_to_fix_status(const struct ubx_msg_nav_pvt *pvt)
{
	switch (pvt->fix_type) {
	case UBX_MSG_NAV_PVT_FIX_TYPE_NO_FIX:
	case UBX_MSG_NAV_PVT_FIX_TYPE_TIME_ONLY:
		return GNSS_FIX_STATUS_NO_FIX;
	case UBX_MSG_NAV_PVT_FIX_TYPE_DEAD_RECKONING:
		return GNSS_FIX_STATUS_ESTIMATED_FIX;
	default:
		return GNSS_FIX_STATUS_GNSS_FIX;
	}
}

/**
 * @brief Get fix quality from NAV-PVT message
 *
 * @param pvt NAV-PVT message
 *
 * @retval status Zephyr fix quality
 */
static inline enum gnss_fix_quality ubx_nav_pvt_to_fix_quality(const struct ubx_msg_nav_pvt *pvt)
{
	switch (pvt->fix_type) {
	case UBX_MSG_NAV_PVT_FIX_TYPE_NO_FIX:
	case UBX_MSG_NAV_PVT_FIX_TYPE_TIME_ONLY:
		return GNSS_FIX_QUALITY_INVALID;
	case UBX_MSG_NAV_PVT_FIX_TYPE_DEAD_RECKONING:
		return GNSS_FIX_QUALITY_ESTIMATED;
	default:
		return GNSS_FIX_QUALITY_GNSS_SPS;
	}
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_GNSS_UBX_ZEPHYR_H_ */
