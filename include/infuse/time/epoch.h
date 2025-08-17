/**
 * @file
 * @brief Infuse-IoT time based on the GPS epoch
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 * Infuse-IoT uses the GPS epoch for local timekeeping.
 * For simplicity, seconds and subseconds are encoded into a single uint64_t.
 * The top 48 bits are the number of seconds elapsed since the GPS epoch (00:00:00 06/01/1980 UTC).
 * The bottom 16 bits are in units of (1/65536) seconds.
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TIME_EPOCH_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TIME_EPOCH_H_

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/toolchain.h>
#include <zephyr/sys/timeutil.h>

/* Time conversion functions */
#include "epoch_units.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief epoch_time API
 * @defgroup epoch_time_apis Epoch APIs
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
#define INFUSE_EPOCH_TIME_GPS_UNIX_OFFSET_SECONDS_LEAP 18

/**
 * @brief Unix time at the instant of the GPS epoch
 */
#define INFUSE_EPOCH_TIME_GPS_UNIX_OFFSET_SECONDS_BASE 315964800

/**
 * @brief Base tick rate of Infuse-IoT epoch time
 */
#define INFUSE_EPOCH_TIME_TICKS_PER_SEC (UINT16_MAX + 1)

/* Source of time knowledge */
enum epoch_time_source {
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

/** @brief epoch time event callback structure. */
struct epoch_time_cb {
	/** @brief The local reference instant has been updated.
	 *
	 * @param source The time source of the new reference instant
	 * @param old Old reference instant
	 * @param new New reference instant
	 * @param user_ctx User context pointer
	 */
	void (*reference_time_updated)(enum epoch_time_source source,
				       struct timeutil_sync_instant old,
				       struct timeutil_sync_instant new, void *user_ctx);

	/* User provided context pointer */
	void *user_ctx;

	sys_snode_t node;
};

/**
 * @brief Register to be notified of epoch time events
 *
 * @param cb Callback struct to register
 */
void epoch_time_register_callback(struct epoch_time_cb *cb);

/**
 * @brief Determine whether a given time source should be trusted
 *
 * @param source Time source
 * @param recovered_ok Should a time source recovered across a reboot be trusted
 * @return true Time source can be trusted
 * @return false Time source should not be trusted
 */
static inline bool epoch_time_trusted_source(enum epoch_time_source source, bool recovered_ok)
{
	enum epoch_time_source base = source & ~TIME_SOURCE_RECOVERED;
	bool recovered = !!(source & TIME_SOURCE_RECOVERED);
	bool base_good = IN_RANGE(base, TIME_SOURCE_NONE + 1, TIME_SOURCE_INVALID - 1);

	return base_good && (!recovered || recovered_ok);
}

/**
 * @brief Extracts epoch seconds from a complete epoch time
 *
 * @param epoch_time Complete epoch time
 * @retval seconds Number of seconds since GPS epoch
 */
static inline uint64_t epoch_time_seconds(uint64_t epoch_time)
{
	return epoch_time >> 16;
}

/**
 * @brief Extracts epoch subseconds from a complete epoch time
 *
 * @param epoch_time Complete epoch time
 * @retval subseconds Fractional component of time since GPS epoch
 */
static inline uint16_t epoch_time_subseconds(uint64_t epoch_time)
{
	return epoch_time & 0xFFFF;
}

/**
 * @brief Extracts epoch milliseconds from a complete epoch time
 *
 * @param epoch_time Complete epoch time
 * @retval milliseconds Fractional component of time since GPS epoch
 */
static inline uint16_t epoch_time_milliseconds(uint64_t epoch_time)
{
	return k_epoch_to_ms_near32(epoch_time_subseconds(epoch_time));
}

/**
 * @brief Convert seconds and subseconds to complete epoch time
 *
 * @param seconds Number of seconds since GPS epoch
 * @param subseconds Fractional component of time since GPS epoch
 * @retval epoch_time Complete epoch time
 */
static inline uint64_t epoch_time_from(uint64_t seconds, uint16_t subseconds)
{
	return (seconds << 16) | subseconds;
}

/**
 * @brief Convert GPS time format to complete epoch time
 *
 * @param week GPS week counter
 * @param week_seconds GPS seconds of week
 * @param subseconds Fractional component of time since GPS epoch
 * @retval epoch_time Complete epoch time
 */
static inline uint64_t epoch_time_from_gps(uint16_t week, uint32_t week_seconds,
					   uint16_t subseconds)
{
	uint64_t seconds = (SECONDS_PER_WEEK * (uint64_t)week) + week_seconds;

	return epoch_time_from(seconds, subseconds);
}

/**
 * @brief Convert epoch time to current unix time
 *
 * @warning To simplify the implementation, this function is only guaranteed to be correct for
 *          times since the last leap second (31st December 2016) until the next leap second
 *          (unknown).
 *
 * @param epoch_time Complete epoch time
 * @retval unix_time Current unix time
 */
static inline uint32_t unix_time_from_epoch(uint64_t epoch_time)
{
	return epoch_time_seconds(epoch_time) + INFUSE_EPOCH_TIME_GPS_UNIX_OFFSET_SECONDS_BASE -
	       INFUSE_EPOCH_TIME_GPS_UNIX_OFFSET_SECONDS_LEAP;
}

/**
 * @brief Convert unix time to current epoch time
 *
 * @warning To simplify the implementation, this function is only guaranteed to be correct for
 *          times since the last leap second (31st December 2016) until the next leap second
 *          (unknown).
 *
 * @param unix_time Current unix time
 * @param subseconds Fractional component of unix time
 * @retval epoch_time Complete epoch time
 */
static inline uint64_t epoch_time_from_unix(uint32_t unix_time, uint16_t subseconds)
{
	uint64_t epoch_seconds = unix_time - INFUSE_EPOCH_TIME_GPS_UNIX_OFFSET_SECONDS_BASE +
				 INFUSE_EPOCH_TIME_GPS_UNIX_OFFSET_SECONDS_LEAP;

	return epoch_time_from(epoch_seconds, subseconds);
}

/**
 * @brief Get a tick count associated with a epoch time
 *
 * @param epoch_time epoch time
 *
 * @return uint64_t Equivalent time in ticks
 */
uint64_t ticks_from_epoch_time(uint64_t epoch_time);

/**
 * @brief Get the epoch time associated with a local uptime
 *
 * @param ticks Kernel tick count
 *
 * @return uint64_t Equivalent time in epoch time
 */
uint64_t epoch_time_from_ticks(uint64_t ticks);

/**
 * @brief Get the epoch time period associated with an array
 *
 * @param array_ticks Ticks between first and last sample in array
 * @param array_num Number of samples in array
 *
 * @return uint32_t Equivalent period in epoch time units
 */
uint32_t epoch_period_from_array_ticks(uint64_t array_ticks, uint16_t array_num);

/**
 * @brief Get the current epoch time
 *
 * @retval epoch_time Complete epoch time
 */
static inline uint64_t epoch_time_now(void)
{
	return epoch_time_from_ticks(k_uptime_ticks());
}

/**
 * @brief Convert a epoch time to a unix time calendar
 *
 * @note Output depends on current leap seconds count, and is therefore only
 *       valid until the next leap second change.
 *
 * @param epoch_time Complete epoch time
 * @param calendar Output calendar
 */
void epoch_time_unix_calendar(uint64_t epoch_time, struct tm *calendar);

/**
 * @brief Get the current source of epoch time knowledge
 *
 * @return enum epoch_time_source Current time source
 */
enum epoch_time_source epoch_time_get_source(void);

/**
 * @brief Set the local to epoch time reference instant
 *
 * @param source Source of the reference instant
 * @param reference Same instant in local and epoch time bases
 *
 * @retval 0 on success
 * @retval -EINVAL if reference instant is invalid
 */
int epoch_time_set_reference(enum epoch_time_source source,
			     struct timeutil_sync_instant *reference);

/**
 * @brief Query how many seconds ago the reference instant was set
 *
 * @retval UINT32_MAX if not yet set
 * @retval seconds since reference instant was set
 */
uint32_t epoch_time_reference_age(void);

/**
 * @brief Determine the epoch time shift due to moving from @a ref_a to @a ref_b
 *
 * @param ref_a Original reference point
 * @param ref_b Updated reference point
 * @param epoch_shift Time shift from updating reference points
 *
 * @retval 0 On success
 * @retval -EINVAL If either sync instant is invalid
 */
int epoch_time_reference_shift(const struct timeutil_sync_instant *ref_a,
			       const struct timeutil_sync_instant *ref_b, int64_t *epoch_shift);

#ifdef CONFIG_ZTEST

/**
 * @brief Reset time knowledge to default
 */
void epoch_time_reset(void);

#endif /* CONFIG_ZTEST */

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TIME_EPOCH_H_ */
