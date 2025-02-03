/**
 * @file
 * @brief Emulated GNSS driver control
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_GNSS_GNSS_EMUL_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_GNSS_GNSS_EMUL_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief gnss_emul API
 * @defgroup gnss_emul_apis emulated GNSS APIs
 * @{
 */

/**
 * @brief Configure the currently output PVT message
 *
 * @param dev Emulated GNSS device
 * @param latitude Latitude (scaled by 1e7)
 * @param longitude Longitude (scaled by 1e7)
 * @param height Height (mm)
 * @param h_acc Horizontal accuracy (mm)
 * @param v_acc Vertical accuracy (mm)
 * @param t_acc Time accuracy (nanoseconds)
 * @param p_dop Position dilution of precision
 * @param num_sv Number of satellite vehicles
 */
void emul_gnss_pvt_configure(const struct device *dev, int32_t latitude, int32_t longitude,
			     int32_t height, uint32_t h_acc, uint32_t v_acc, uint32_t t_acc,
			     uint16_t p_dop, uint8_t num_sv);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_GNSS_GNSS_EMUL_H_ */
