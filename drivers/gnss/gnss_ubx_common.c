/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/sys/util.h>
#include <zephyr/drivers/gnss.h>

#include <infuse/gnss/ubx/defines.h>

static const char *const ubx_gnss_names[] = {
	[UBX_GNSS_ID_GPS] = "GPS",         [UBX_GNSS_ID_SBAS] = "SBAS",
	[UBX_GNSS_ID_GALILEO] = "GALILEO", [UBX_GNSS_ID_BEIDOU] = "BEIDOU",
	[UBX_GNSS_ID_NAVIC] = "NAVIC",     [UBX_GNSS_ID_QZSS] = "QZSS",
	[UBX_GNSS_ID_GLONASS] = "GLONASS",
};

int ubx_gnss_id_to_gnss_system(enum ubx_gnss_id gnss_id)
{
	switch (gnss_id) {
	case UBX_GNSS_ID_GPS:
		return GNSS_SYSTEM_GPS;
	case UBX_GNSS_ID_SBAS:
		return GNSS_SYSTEM_SBAS;
	case UBX_GNSS_ID_GALILEO:
		return GNSS_SYSTEM_GALILEO;
	case UBX_GNSS_ID_BEIDOU:
		return GNSS_SYSTEM_BEIDOU;
	case UBX_GNSS_ID_QZSS:
		return GNSS_SYSTEM_QZSS;
	case UBX_GNSS_ID_GLONASS:
		return GNSS_SYSTEM_GLONASS;
	case UBX_GNSS_ID_NAVIC:
		return GNSS_SYSTEM_IRNSS;
	default:
		return -EINVAL;
	}
}

int gnss_system_to_ubx_gnss_id(enum gnss_system gnss_system)
{
	switch (gnss_system) {
	case GNSS_SYSTEM_GPS:
		return UBX_GNSS_ID_GPS;
	case GNSS_SYSTEM_SBAS:
		return UBX_GNSS_ID_SBAS;
	case GNSS_SYSTEM_GALILEO:
		return UBX_GNSS_ID_GALILEO;
	case GNSS_SYSTEM_BEIDOU:
		return UBX_GNSS_ID_BEIDOU;
	case GNSS_SYSTEM_QZSS:
		return UBX_GNSS_ID_QZSS;
	case GNSS_SYSTEM_GLONASS:
		return UBX_GNSS_ID_GLONASS;
	case GNSS_SYSTEM_IRNSS:
		return UBX_GNSS_ID_NAVIC;
	default:
		return -EINVAL;
	}
}

const char *ubx_gnss_id_name(enum ubx_gnss_id gnss_id)
{
	if (gnss_id > ARRAY_SIZE(ubx_gnss_names)) {
		return "N/A";
	}
	return ubx_gnss_names[gnss_id];
}
