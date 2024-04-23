/**
 * @file
 * @brief EIS civil time based on the GPS epoch
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 *
 * @details
 * EIS uses the GPS epoch for local civil timekeeping.
 * For simplicity, seconds and subseconds are encoded into a single uint64_t.
 * The top 48 bits are the number of seconds elapsed since the GPS epoch (00:00:00 01/01/1980 UTC).
 * The bottom 16 bits are in units of (1/65536) seconds.
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TIME_CIVIL_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TIME_CIVIL_H_

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/toolchain.h>
#include <zephyr/sys/timeutil.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief civil_time API
 * @defgroup CIVIL_apis CIVIL APIs
 * @{
 */

#define SECONDS_PER_MINUTE (60)
#define SECONDS_PER_HOUR   (60 * SECONDS_PER_MINUTE)
#define SECONDS_PER_DAY    (24 * SECONDS_PER_HOUR)
#define SECONDS_PER_WEEK   (7 * SECONDS_PER_DAY)

/**
 * @brief Current offset between GPS and UNIX timestamps
 *
 * @warning This value will be incorrect after the next leap-second
 */
#define EIS_CIVIL_TIME_GPS_UNIX_OFFSET_SECONDS_LEAP 18

/**
 * @brief Unix time at the instant of the GPS epoch
 */
#define EIS_CIVIL_TIME_GPS_UNIX_OFFSET_SECONDS_BASE 315964800

/**
 * @brief Base tick rate of EIS civil time
 */
#define EIS_CIVIL_TIME_TICKS_PER_SEC (UINT16_MAX + 1)

/* Source of time knowledge */
enum civil_time_source {
	/* No time knowledge */
	TIME_SOURCE_NONE = 0,
	/* Time from GNSS constellation (GPS, Beidou, etc) */
	TIME_SOURCE_GNSS,
	/* Time from Network Time Protocol (IP) */
	TIME_SOURCE_NTP,
	/* Time directly set by Remote Procedure Call */
	TIME_SOURCE_RPC,
	/* Unknown time source value */
	TIME_SOURCE_INVALID,
	/* Time has been preserved across a reboot */
	TIME_SOURCE_RECOVERED = 0x80,
} __packed;

/**
 * @brief Extracts epoch seconds from a complete civil time
 *
 * @param civil_time Complete civil time
 * @retval seconds Number of seconds since GPS epoch
 */
static inline uint64_t civil_time_seconds(uint64_t civil_time)
{
	return civil_time >> 16;
}

/**
 * @brief Extracts epoch subseconds from a complete civil time
 *
 * @param civil_time Complete civil time
 * @retval subseconds Fractional component of time since GPS epoch
 */
static inline uint16_t civil_time_subseconds(uint64_t civil_time)
{
	return civil_time & 0xFFFF;
}

/**
 * @brief Convert seconds and subseconds to complete civil time
 *
 * @param seconds Number of seconds since GPS epoch
 * @param subseconds Fractional component of time since GPS epoch
 * @retval civil_time Complete civil time
 */
static inline uint64_t civil_time_from(uint64_t seconds, uint16_t subseconds)
{
	return (seconds << 16) | subseconds;
}

/**
 * @brief Convert GPS time format to complete civil time
 *
 * @param week GPS week counter
 * @param week_seconds GPS seconds of week
 * @param subseconds Fractional component of time since GPS epoch
 * @retval civil_time Complete civil time
 */
static inline uint64_t civil_time_from_gps(uint16_t week, uint32_t week_seconds, uint16_t subseconds)
{
	uint64_t seconds = (SECONDS_PER_WEEK * (uint64_t)week) + week_seconds;

	return civil_time_from(seconds, subseconds);
}

/**
 * @brief Convert civil time to current unix time
 *
 * @warning To simplify the implementation, this function is only guaranteed to be correct for
 *          times since the last leap second (31st December 2016) until the next leap second
 *          (unknown).
 *
 * @param civil_time Complete civil time
 * @retval unix_time Current unix time
 */
static inline uint32_t unix_time_from_civil(uint64_t civil_time)
{
	return civil_time_seconds(civil_time) + EIS_CIVIL_TIME_GPS_UNIX_OFFSET_SECONDS_BASE -
	       EIS_CIVIL_TIME_GPS_UNIX_OFFSET_SECONDS_LEAP;
}

/**
 * @brief Convert unix time to current civil time
 *
 * @warning To simplify the implementation, this function is only guaranteed to be correct for
 *          times since the last leap second (31st December 2016) until the next leap second
 *          (unknown).
 *
 * @param unix_time Current unix time
 * @param subseconds Fractional component of unix time
 * @retval civil_time Complete civil time
 */
static inline uint64_t civil_time_from_unix(uint32_t unix_time, uint16_t subseconds)
{
	uint64_t civil_seconds =
		unix_time - EIS_CIVIL_TIME_GPS_UNIX_OFFSET_SECONDS_BASE + EIS_CIVIL_TIME_GPS_UNIX_OFFSET_SECONDS_LEAP;

	return civil_time_from(civil_seconds, subseconds);
}

/**
 * @brief Get the civil time associated with a local uptime
 *
 * @param ticks Kernel tick count
 * @return uint64_t
 */
uint64_t civil_time_from_ticks(uint64_t ticks);

/**
 * @brief Get the current civil time
 *
 * @retval civil_time Complete civil time
 */
static inline uint64_t civil_time_now(void)
{
	return civil_time_from_ticks(k_uptime_ticks());
}

/**
 * @brief Get the current source of civil time knowledge
 *
 * @return enum civil_time_source Current time source
 */
enum civil_time_source civil_time_get_source(void);

/**
 * @brief Set the local to civil time reference instant
 *
 * @param source Source of the reference instant
 * @param reference Same instant in local and civil time bases
 *
 * @retval 0 on success
 */
int civil_time_set_reference(enum civil_time_source source, struct timeutil_sync_instant *reference);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TIME_CIVIL_H_ */
