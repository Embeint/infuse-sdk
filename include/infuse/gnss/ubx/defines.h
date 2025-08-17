/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * Common UBX defines not related to the messaging protocol.
 */

#ifndef INFUSE_GNSS_UBX_DEFINES_H_
#define INFUSE_GNSS_UBX_DEFINES_H_

#include <zephyr/net_buf.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/gnss/ubx/protocol.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup ubx_defines UBX Common Defines
 * @{
 */

enum ubx_gnss_id {
	/** Global Positioning System (GPS) */
	UBX_GNSS_ID_GPS = 0,
	/** Satellite-Based Augmentation System (SBAS) */
	UBX_GNSS_ID_SBAS = 1,
	/** Galileo */
	UBX_GNSS_ID_GALILEO = 2,
	/** BeiDou Navigation Satellite System */
	UBX_GNSS_ID_BEIDOU = 3,
	/** Quasi-Zenith Satellite System (QZSS) */
	UBX_GNSS_ID_QZSS = 5,
	/** GLObal NAvigation Satellite System (GLONASS) */
	UBX_GNSS_ID_GLONASS = 6,
	/** Indian Regional Navigation Satellite System (IRNSS/NAVIC) */
	UBX_GNSS_ID_NAVIC = 7,
};

/**
 * @brief Convert UBX GNSS ID to name
 *
 * @param gnss_id UBX GNSS ID
 *
 * @retval "N/A" On invalid @a gnss_id
 * @retval Name Human readable GNSS network name otherwise
 */
const char *ubx_gnss_id_name(enum ubx_gnss_id gnss_id);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_GNSS_UBX_DEFINES_H_ */
