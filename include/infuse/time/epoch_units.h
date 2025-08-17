/**
 * @file
 * @brief Conversion between epoch ticks and other units
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * Equivalent functionality to `zephyr/sys/time_units.h` for
 * epoch times. Generated with the script from that file.
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TIME_EPOCH_UNITS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TIME_EPOCH_UNITS_H_

#include <zephyr/sys/time_units.h>

#define Z_HZ_epoch INFUSE_EPOCH_TIME_TICKS_PER_SEC

/** @brief Convert seconds to epoch ticks. 32 bits. Truncates.
 *
 * Converts time values in seconds to epoch ticks.
 * Computes result in 32 bit precision.
 * Truncates to the next lowest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in seconds. uint64_t
 *
 * @return The converted time value in epoch ticks. uint32_t
 */
#define k_sec_to_epoch_floor32(t) z_tmcvt_32(t, Z_HZ_sec, Z_HZ_epoch, true, false, false)

/** @brief Convert seconds to epoch ticks. 64 bits. Truncates.
 *
 * Converts time values in seconds to epoch ticks.
 * Computes result in 64 bit precision.
 * Truncates to the next lowest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in seconds. uint64_t
 *
 * @return The converted time value in epoch ticks. uint64_t
 */
#define k_sec_to_epoch_floor64(t) z_tmcvt_64(t, Z_HZ_sec, Z_HZ_epoch, true, false, false)

/** @brief Convert seconds to epoch ticks. 32 bits. Round nearest.
 *
 * Converts time values in seconds to epoch ticks.
 * Computes result in 32 bit precision.
 * Rounds to the nearest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in seconds. uint64_t
 *
 * @return The converted time value in epoch ticks. uint32_t
 */
#define k_sec_to_epoch_near32(t) z_tmcvt_32(t, Z_HZ_sec, Z_HZ_epoch, true, false, true)

/** @brief Convert seconds to epoch ticks. 64 bits. Round nearest.
 *
 * Converts time values in seconds to epoch ticks.
 * Computes result in 64 bit precision.
 * Rounds to the nearest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in seconds. uint64_t
 *
 * @return The converted time value in epoch ticks. uint64_t
 */
#define k_sec_to_epoch_near64(t) z_tmcvt_64(t, Z_HZ_sec, Z_HZ_epoch, true, false, true)

/** @brief Convert seconds to epoch ticks. 32 bits. Rounds up.
 *
 * Converts time values in seconds to epoch ticks.
 * Computes result in 32 bit precision.
 * Rounds up to the next highest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in seconds. uint64_t
 *
 * @return The converted time value in epoch ticks. uint32_t
 */
#define k_sec_to_epoch_ceil32(t) z_tmcvt_32(t, Z_HZ_sec, Z_HZ_epoch, true, true, false)

/** @brief Convert seconds to epoch ticks. 64 bits. Rounds up.
 *
 * Converts time values in seconds to epoch ticks.
 * Computes result in 64 bit precision.
 * Rounds up to the next highest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in seconds. uint64_t
 *
 * @return The converted time value in epoch ticks. uint64_t
 */
#define k_sec_to_epoch_ceil64(t) z_tmcvt_64(t, Z_HZ_sec, Z_HZ_epoch, true, true, false)

/** @brief Convert milliseconds to epoch ticks. 32 bits. Truncates.
 *
 * Converts time values in milliseconds to epoch ticks.
 * Computes result in 32 bit precision.
 * Truncates to the next lowest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in milliseconds. uint64_t
 *
 * @return The converted time value in epoch ticks. uint32_t
 */
#define k_ms_to_epoch_floor32(t) z_tmcvt_32(t, Z_HZ_ms, Z_HZ_epoch, true, false, false)

/** @brief Convert milliseconds to epoch ticks. 64 bits. Truncates.
 *
 * Converts time values in milliseconds to epoch ticks.
 * Computes result in 64 bit precision.
 * Truncates to the next lowest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in milliseconds. uint64_t
 *
 * @return The converted time value in epoch ticks. uint64_t
 */
#define k_ms_to_epoch_floor64(t) z_tmcvt_64(t, Z_HZ_ms, Z_HZ_epoch, true, false, false)

/** @brief Convert milliseconds to epoch ticks. 32 bits. Round nearest.
 *
 * Converts time values in milliseconds to epoch ticks.
 * Computes result in 32 bit precision.
 * Rounds to the nearest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in milliseconds. uint64_t
 *
 * @return The converted time value in epoch ticks. uint32_t
 */
#define k_ms_to_epoch_near32(t) z_tmcvt_32(t, Z_HZ_ms, Z_HZ_epoch, true, false, true)

/** @brief Convert milliseconds to epoch ticks. 64 bits. Round nearest.
 *
 * Converts time values in milliseconds to epoch ticks.
 * Computes result in 64 bit precision.
 * Rounds to the nearest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in milliseconds. uint64_t
 *
 * @return The converted time value in epoch ticks. uint64_t
 */
#define k_ms_to_epoch_near64(t) z_tmcvt_64(t, Z_HZ_ms, Z_HZ_epoch, true, false, true)

/** @brief Convert milliseconds to epoch ticks. 32 bits. Rounds up.
 *
 * Converts time values in milliseconds to epoch ticks.
 * Computes result in 32 bit precision.
 * Rounds up to the next highest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in milliseconds. uint64_t
 *
 * @return The converted time value in epoch ticks. uint32_t
 */
#define k_ms_to_epoch_ceil32(t) z_tmcvt_32(t, Z_HZ_ms, Z_HZ_epoch, true, true, false)

/** @brief Convert milliseconds to epoch ticks. 64 bits. Rounds up.
 *
 * Converts time values in milliseconds to epoch ticks.
 * Computes result in 64 bit precision.
 * Rounds up to the next highest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in milliseconds. uint64_t
 *
 * @return The converted time value in epoch ticks. uint64_t
 */
#define k_ms_to_epoch_ceil64(t) z_tmcvt_64(t, Z_HZ_ms, Z_HZ_epoch, true, true, false)

/** @brief Convert microseconds to epoch ticks. 32 bits. Truncates.
 *
 * Converts time values in microseconds to epoch ticks.
 * Computes result in 32 bit precision.
 * Truncates to the next lowest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in microseconds. uint64_t
 *
 * @return The converted time value in epoch ticks. uint32_t
 */
#define k_us_to_epoch_floor32(t) z_tmcvt_32(t, Z_HZ_us, Z_HZ_epoch, true, false, false)

/** @brief Convert microseconds to epoch ticks. 64 bits. Truncates.
 *
 * Converts time values in microseconds to epoch ticks.
 * Computes result in 64 bit precision.
 * Truncates to the next lowest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in microseconds. uint64_t
 *
 * @return The converted time value in epoch ticks. uint64_t
 */
#define k_us_to_epoch_floor64(t) z_tmcvt_64(t, Z_HZ_us, Z_HZ_epoch, true, false, false)

/** @brief Convert microseconds to epoch ticks. 32 bits. Round nearest.
 *
 * Converts time values in microseconds to epoch ticks.
 * Computes result in 32 bit precision.
 * Rounds to the nearest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in microseconds. uint64_t
 *
 * @return The converted time value in epoch ticks. uint32_t
 */
#define k_us_to_epoch_near32(t) z_tmcvt_32(t, Z_HZ_us, Z_HZ_epoch, true, false, true)

/** @brief Convert microseconds to epoch ticks. 64 bits. Round nearest.
 *
 * Converts time values in microseconds to epoch ticks.
 * Computes result in 64 bit precision.
 * Rounds to the nearest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in microseconds. uint64_t
 *
 * @return The converted time value in epoch ticks. uint64_t
 */
#define k_us_to_epoch_near64(t) z_tmcvt_64(t, Z_HZ_us, Z_HZ_epoch, true, false, true)

/** @brief Convert microseconds to epoch ticks. 32 bits. Rounds up.
 *
 * Converts time values in microseconds to epoch ticks.
 * Computes result in 32 bit precision.
 * Rounds up to the next highest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in microseconds. uint64_t
 *
 * @return The converted time value in epoch ticks. uint32_t
 */
#define k_us_to_epoch_ceil32(t) z_tmcvt_32(t, Z_HZ_us, Z_HZ_epoch, true, true, false)

/** @brief Convert microseconds to epoch ticks. 64 bits. Rounds up.
 *
 * Converts time values in microseconds to epoch ticks.
 * Computes result in 64 bit precision.
 * Rounds up to the next highest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in microseconds. uint64_t
 *
 * @return The converted time value in epoch ticks. uint64_t
 */
#define k_us_to_epoch_ceil64(t) z_tmcvt_64(t, Z_HZ_us, Z_HZ_epoch, true, true, false)

/** @brief Convert nanoseconds to epoch ticks. 32 bits. Truncates.
 *
 * Converts time values in nanoseconds to epoch ticks.
 * Computes result in 32 bit precision.
 * Truncates to the next lowest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in nanoseconds. uint64_t
 *
 * @return The converted time value in epoch ticks. uint32_t
 */
#define k_ns_to_epoch_floor32(t) z_tmcvt_32(t, Z_HZ_ns, Z_HZ_epoch, true, false, false)

/** @brief Convert nanoseconds to epoch ticks. 64 bits. Truncates.
 *
 * Converts time values in nanoseconds to epoch ticks.
 * Computes result in 64 bit precision.
 * Truncates to the next lowest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in nanoseconds. uint64_t
 *
 * @return The converted time value in epoch ticks. uint64_t
 */
#define k_ns_to_epoch_floor64(t) z_tmcvt_64(t, Z_HZ_ns, Z_HZ_epoch, true, false, false)

/** @brief Convert nanoseconds to epoch ticks. 32 bits. Round nearest.
 *
 * Converts time values in nanoseconds to epoch ticks.
 * Computes result in 32 bit precision.
 * Rounds to the nearest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in nanoseconds. uint64_t
 *
 * @return The converted time value in epoch ticks. uint32_t
 */
#define k_ns_to_epoch_near32(t) z_tmcvt_32(t, Z_HZ_ns, Z_HZ_epoch, true, false, true)

/** @brief Convert nanoseconds to epoch ticks. 64 bits. Round nearest.
 *
 * Converts time values in nanoseconds to epoch ticks.
 * Computes result in 64 bit precision.
 * Rounds to the nearest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in nanoseconds. uint64_t
 *
 * @return The converted time value in epoch ticks. uint64_t
 */
#define k_ns_to_epoch_near64(t) z_tmcvt_64(t, Z_HZ_ns, Z_HZ_epoch, true, false, true)

/** @brief Convert nanoseconds to epoch ticks. 32 bits. Rounds up.
 *
 * Converts time values in nanoseconds to epoch ticks.
 * Computes result in 32 bit precision.
 * Rounds up to the next highest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in nanoseconds. uint64_t
 *
 * @return The converted time value in epoch ticks. uint32_t
 */
#define k_ns_to_epoch_ceil32(t) z_tmcvt_32(t, Z_HZ_ns, Z_HZ_epoch, true, true, false)

/** @brief Convert nanoseconds to epoch ticks. 64 bits. Rounds up.
 *
 * Converts time values in nanoseconds to epoch ticks.
 * Computes result in 64 bit precision.
 * Rounds up to the next highest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in nanoseconds. uint64_t
 *
 * @return The converted time value in epoch ticks. uint64_t
 */
#define k_ns_to_epoch_ceil64(t) z_tmcvt_64(t, Z_HZ_ns, Z_HZ_epoch, true, true, false)

/** @brief Convert ticks to epoch ticks. 32 bits. Truncates.
 *
 * Converts time values in ticks to epoch ticks.
 * Computes result in 32 bit precision.
 * Truncates to the next lowest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in ticks. uint64_t
 *
 * @return The converted time value in epoch ticks. uint32_t
 */
#define k_ticks_to_epoch_floor32(t) z_tmcvt_32(t, Z_HZ_ticks, Z_HZ_epoch, true, false, false)

/** @brief Convert ticks to epoch ticks. 64 bits. Truncates.
 *
 * Converts time values in ticks to epoch ticks.
 * Computes result in 64 bit precision.
 * Truncates to the next lowest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in ticks. uint64_t
 *
 * @return The converted time value in epoch ticks. uint64_t
 */
#define k_ticks_to_epoch_floor64(t) z_tmcvt_64(t, Z_HZ_ticks, Z_HZ_epoch, true, false, false)

/** @brief Convert ticks to epoch ticks. 32 bits. Round nearest.
 *
 * Converts time values in ticks to epoch ticks.
 * Computes result in 32 bit precision.
 * Rounds to the nearest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in ticks. uint64_t
 *
 * @return The converted time value in epoch ticks. uint32_t
 */
#define k_ticks_to_epoch_near32(t) z_tmcvt_32(t, Z_HZ_ticks, Z_HZ_epoch, true, false, true)

/** @brief Convert ticks to epoch ticks. 64 bits. Round nearest.
 *
 * Converts time values in ticks to epoch ticks.
 * Computes result in 64 bit precision.
 * Rounds to the nearest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in ticks. uint64_t
 *
 * @return The converted time value in epoch ticks. uint64_t
 */
#define k_ticks_to_epoch_near64(t) z_tmcvt_64(t, Z_HZ_ticks, Z_HZ_epoch, true, false, true)

/** @brief Convert ticks to epoch ticks. 32 bits. Rounds up.
 *
 * Converts time values in ticks to epoch ticks.
 * Computes result in 32 bit precision.
 * Rounds up to the next highest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in ticks. uint64_t
 *
 * @return The converted time value in epoch ticks. uint32_t
 */
#define k_ticks_to_epoch_ceil32(t) z_tmcvt_32(t, Z_HZ_ticks, Z_HZ_epoch, true, true, false)

/** @brief Convert ticks to epoch ticks. 64 bits. Rounds up.
 *
 * Converts time values in ticks to epoch ticks.
 * Computes result in 64 bit precision.
 * Rounds up to the next highest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in ticks. uint64_t
 *
 * @return The converted time value in epoch ticks. uint64_t
 */
#define k_ticks_to_epoch_ceil64(t) z_tmcvt_64(t, Z_HZ_ticks, Z_HZ_epoch, true, true, false)

/** @brief Convert epoch ticks to seconds. 32 bits. Truncates.
 *
 * Converts time values in epoch ticks to seconds.
 * Computes result in 32 bit precision.
 * Truncates to the next lowest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in seconds. uint32_t
 */
#define k_epoch_to_sec_floor32(t) z_tmcvt_32(t, Z_HZ_epoch, Z_HZ_sec, true, false, false)

/** @brief Convert epoch ticks to seconds. 64 bits. Truncates.
 *
 * Converts time values in epoch ticks to seconds.
 * Computes result in 64 bit precision.
 * Truncates to the next lowest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in seconds. uint64_t
 */
#define k_epoch_to_sec_floor64(t) z_tmcvt_64(t, Z_HZ_epoch, Z_HZ_sec, true, false, false)

/** @brief Convert epoch ticks to seconds. 32 bits. Round nearest.
 *
 * Converts time values in epoch ticks to seconds.
 * Computes result in 32 bit precision.
 * Rounds to the nearest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in seconds. uint32_t
 */
#define k_epoch_to_sec_near32(t) z_tmcvt_32(t, Z_HZ_epoch, Z_HZ_sec, true, false, true)

/** @brief Convert epoch ticks to seconds. 64 bits. Round nearest.
 *
 * Converts time values in epoch ticks to seconds.
 * Computes result in 64 bit precision.
 * Rounds to the nearest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in seconds. uint64_t
 */
#define k_epoch_to_sec_near64(t) z_tmcvt_64(t, Z_HZ_epoch, Z_HZ_sec, true, false, true)

/** @brief Convert epoch ticks to seconds. 32 bits. Rounds up.
 *
 * Converts time values in epoch ticks to seconds.
 * Computes result in 32 bit precision.
 * Rounds up to the next highest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in seconds. uint32_t
 */
#define k_epoch_to_sec_ceil32(t) z_tmcvt_32(t, Z_HZ_epoch, Z_HZ_sec, true, true, false)

/** @brief Convert epoch ticks to seconds. 64 bits. Rounds up.
 *
 * Converts time values in epoch ticks to seconds.
 * Computes result in 64 bit precision.
 * Rounds up to the next highest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in seconds. uint64_t
 */
#define k_epoch_to_sec_ceil64(t) z_tmcvt_64(t, Z_HZ_epoch, Z_HZ_sec, true, true, false)

/** @brief Convert epoch ticks to milliseconds. 32 bits. Truncates.
 *
 * Converts time values in epoch ticks to milliseconds.
 * Computes result in 32 bit precision.
 * Truncates to the next lowest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in milliseconds. uint32_t
 */
#define k_epoch_to_ms_floor32(t) z_tmcvt_32(t, Z_HZ_epoch, Z_HZ_ms, true, false, false)

/** @brief Convert epoch ticks to milliseconds. 64 bits. Truncates.
 *
 * Converts time values in epoch ticks to milliseconds.
 * Computes result in 64 bit precision.
 * Truncates to the next lowest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in milliseconds. uint64_t
 */
#define k_epoch_to_ms_floor64(t) z_tmcvt_64(t, Z_HZ_epoch, Z_HZ_ms, true, false, false)

/** @brief Convert epoch ticks to milliseconds. 32 bits. Round nearest.
 *
 * Converts time values in epoch ticks to milliseconds.
 * Computes result in 32 bit precision.
 * Rounds to the nearest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in milliseconds. uint32_t
 */
#define k_epoch_to_ms_near32(t) z_tmcvt_32(t, Z_HZ_epoch, Z_HZ_ms, true, false, true)

/** @brief Convert epoch ticks to milliseconds. 64 bits. Round nearest.
 *
 * Converts time values in epoch ticks to milliseconds.
 * Computes result in 64 bit precision.
 * Rounds to the nearest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in milliseconds. uint64_t
 */
#define k_epoch_to_ms_near64(t) z_tmcvt_64(t, Z_HZ_epoch, Z_HZ_ms, true, false, true)

/** @brief Convert epoch ticks to milliseconds. 32 bits. Rounds up.
 *
 * Converts time values in epoch ticks to milliseconds.
 * Computes result in 32 bit precision.
 * Rounds up to the next highest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in milliseconds. uint32_t
 */
#define k_epoch_to_ms_ceil32(t) z_tmcvt_32(t, Z_HZ_epoch, Z_HZ_ms, true, true, false)

/** @brief Convert epoch ticks to milliseconds. 64 bits. Rounds up.
 *
 * Converts time values in epoch ticks to milliseconds.
 * Computes result in 64 bit precision.
 * Rounds up to the next highest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in milliseconds. uint64_t
 */
#define k_epoch_to_ms_ceil64(t) z_tmcvt_64(t, Z_HZ_epoch, Z_HZ_ms, true, true, false)

/** @brief Convert epoch ticks to microseconds. 32 bits. Truncates.
 *
 * Converts time values in epoch ticks to microseconds.
 * Computes result in 32 bit precision.
 * Truncates to the next lowest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in microseconds. uint32_t
 */
#define k_epoch_to_us_floor32(t) z_tmcvt_32(t, Z_HZ_epoch, Z_HZ_us, true, false, false)

/** @brief Convert epoch ticks to microseconds. 64 bits. Truncates.
 *
 * Converts time values in epoch ticks to microseconds.
 * Computes result in 64 bit precision.
 * Truncates to the next lowest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in microseconds. uint64_t
 */
#define k_epoch_to_us_floor64(t) z_tmcvt_64(t, Z_HZ_epoch, Z_HZ_us, true, false, false)

/** @brief Convert epoch ticks to microseconds. 32 bits. Round nearest.
 *
 * Converts time values in epoch ticks to microseconds.
 * Computes result in 32 bit precision.
 * Rounds to the nearest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in microseconds. uint32_t
 */
#define k_epoch_to_us_near32(t) z_tmcvt_32(t, Z_HZ_epoch, Z_HZ_us, true, false, true)

/** @brief Convert epoch ticks to microseconds. 64 bits. Round nearest.
 *
 * Converts time values in epoch ticks to microseconds.
 * Computes result in 64 bit precision.
 * Rounds to the nearest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in microseconds. uint64_t
 */
#define k_epoch_to_us_near64(t) z_tmcvt_64(t, Z_HZ_epoch, Z_HZ_us, true, false, true)

/** @brief Convert epoch ticks to microseconds. 32 bits. Rounds up.
 *
 * Converts time values in epoch ticks to microseconds.
 * Computes result in 32 bit precision.
 * Rounds up to the next highest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in microseconds. uint32_t
 */
#define k_epoch_to_us_ceil32(t) z_tmcvt_32(t, Z_HZ_epoch, Z_HZ_us, true, true, false)

/** @brief Convert epoch ticks to microseconds. 64 bits. Rounds up.
 *
 * Converts time values in epoch ticks to microseconds.
 * Computes result in 64 bit precision.
 * Rounds up to the next highest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in microseconds. uint64_t
 */
#define k_epoch_to_us_ceil64(t) z_tmcvt_64(t, Z_HZ_epoch, Z_HZ_us, true, true, false)

/** @brief Convert epoch ticks to nanoseconds. 32 bits. Truncates.
 *
 * Converts time values in epoch ticks to nanoseconds.
 * Computes result in 32 bit precision.
 * Truncates to the next lowest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in nanoseconds. uint32_t
 */
#define k_epoch_to_ns_floor32(t) z_tmcvt_32(t, Z_HZ_epoch, Z_HZ_ns, true, false, false)

/** @brief Convert epoch ticks to nanoseconds. 64 bits. Truncates.
 *
 * Converts time values in epoch ticks to nanoseconds.
 * Computes result in 64 bit precision.
 * Truncates to the next lowest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in nanoseconds. uint64_t
 */
#define k_epoch_to_ns_floor64(t) z_tmcvt_64(t, Z_HZ_epoch, Z_HZ_ns, true, false, false)

/** @brief Convert epoch ticks to nanoseconds. 32 bits. Round nearest.
 *
 * Converts time values in epoch ticks to nanoseconds.
 * Computes result in 32 bit precision.
 * Rounds to the nearest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in nanoseconds. uint32_t
 */
#define k_epoch_to_ns_near32(t) z_tmcvt_32(t, Z_HZ_epoch, Z_HZ_ns, true, false, true)

/** @brief Convert epoch ticks to nanoseconds. 64 bits. Round nearest.
 *
 * Converts time values in epoch ticks to nanoseconds.
 * Computes result in 64 bit precision.
 * Rounds to the nearest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in nanoseconds. uint64_t
 */
#define k_epoch_to_ns_near64(t) z_tmcvt_64(t, Z_HZ_epoch, Z_HZ_ns, true, false, true)

/** @brief Convert epoch ticks to nanoseconds. 32 bits. Rounds up.
 *
 * Converts time values in epoch ticks to nanoseconds.
 * Computes result in 32 bit precision.
 * Rounds up to the next highest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in nanoseconds. uint32_t
 */
#define k_epoch_to_ns_ceil32(t) z_tmcvt_32(t, Z_HZ_epoch, Z_HZ_ns, true, true, false)

/** @brief Convert epoch ticks to nanoseconds. 64 bits. Rounds up.
 *
 * Converts time values in epoch ticks to nanoseconds.
 * Computes result in 64 bit precision.
 * Rounds up to the next highest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in nanoseconds. uint64_t
 */
#define k_epoch_to_ns_ceil64(t) z_tmcvt_64(t, Z_HZ_epoch, Z_HZ_ns, true, true, false)

/** @brief Convert epoch ticks to ticks. 32 bits. Truncates.
 *
 * Converts time values in epoch ticks to ticks.
 * Computes result in 32 bit precision.
 * Truncates to the next lowest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in ticks. uint32_t
 */
#define k_epoch_to_ticks_floor32(t) z_tmcvt_32(t, Z_HZ_epoch, Z_HZ_ticks, true, false, false)

/** @brief Convert epoch ticks to ticks. 64 bits. Truncates.
 *
 * Converts time values in epoch ticks to ticks.
 * Computes result in 64 bit precision.
 * Truncates to the next lowest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in ticks. uint64_t
 */
#define k_epoch_to_ticks_floor64(t) z_tmcvt_64(t, Z_HZ_epoch, Z_HZ_ticks, true, false, false)

/** @brief Convert epoch ticks to ticks. 32 bits. Round nearest.
 *
 * Converts time values in epoch ticks to ticks.
 * Computes result in 32 bit precision.
 * Rounds to the nearest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in ticks. uint32_t
 */
#define k_epoch_to_ticks_near32(t) z_tmcvt_32(t, Z_HZ_epoch, Z_HZ_ticks, true, false, true)

/** @brief Convert epoch ticks to ticks. 64 bits. Round nearest.
 *
 * Converts time values in epoch ticks to ticks.
 * Computes result in 64 bit precision.
 * Rounds to the nearest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in ticks. uint64_t
 */
#define k_epoch_to_ticks_near64(t) z_tmcvt_64(t, Z_HZ_epoch, Z_HZ_ticks, true, false, true)

/** @brief Convert epoch ticks to ticks. 32 bits. Rounds up.
 *
 * Converts time values in epoch ticks to ticks.
 * Computes result in 32 bit precision.
 * Rounds up to the next highest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in ticks. uint32_t
 */
#define k_epoch_to_ticks_ceil32(t) z_tmcvt_32(t, Z_HZ_epoch, Z_HZ_ticks, true, true, false)

/** @brief Convert epoch ticks to ticks. 64 bits. Rounds up.
 *
 * Converts time values in epoch ticks to ticks.
 * Computes result in 64 bit precision.
 * Rounds up to the next highest output unit.
 *
 * @warning Generated. Do not edit. See above.
 *
 * @param t Source time in epoch ticks. uint64_t
 *
 * @return The converted time value in ticks. uint64_t
 */
#define k_epoch_to_ticks_ceil64(t) z_tmcvt_64(t, Z_HZ_epoch, Z_HZ_ticks, true, true, false)

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TIME_EPOCH_UNITS_H_ */
