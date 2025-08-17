/**
 * @file
 * @brief Infuse validation framework
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_CORE_H_
#define INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_CORE_H_

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup validation Core validation API
 * @{
 */

/* clang-format off */
/**
 * @brief Core validation API reporting function
 *
 * @param system Component being validated
 * @param result Report type
 * @param fmt Report format string
 * @param ... Format string parameters
 */
#define _VALIDATION_REPORT(system, result, fmt, ...)                                               \
	printk("%06d:%s:%s:" fmt "\n", k_uptime_get_32(), system,                                  \
	       result __VA_OPT__(,) __VA_ARGS__)
/* clang-format on */

/**
 * @brief Information report
 *
 * @param system Component being validated
 * @param fmt Report format string
 * @param ... Format string parameters
 */
#define VALIDATION_REPORT_INFO(system, fmt, ...)                                                   \
	_VALIDATION_REPORT(system, "INFO", fmt, __VA_ARGS__)

/**
 * @brief Value report
 *
 * @param system Component being validated
 * @param name Name of the value
 * @param fmt Format specifier for value
 * @param ... Value
 */
#define VALIDATION_REPORT_VALUE(system, name, fmt, ...)                                            \
	printk("%06d:%s:VAL:" name ":" fmt "\n", k_uptime_get_32(), system, __VA_ARGS__)

/**
 * @brief Failure report
 *
 * @param system Component being validated
 * @param fmt Report format string
 * @param ... Format string parameters
 */
#define VALIDATION_REPORT_ERROR(system, fmt, ...)                                                  \
	_VALIDATION_REPORT(system, "ERROR", fmt, __VA_ARGS__)

/**
 * @brief Pass report
 *
 * @param system Component being validated
 * @param fmt Report format string
 * @param ... Format string parameters
 */
#define VALIDATION_REPORT_PASS(system, fmt, ...)                                                   \
	_VALIDATION_REPORT(system, "PASS", fmt, __VA_ARGS__)

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_CORE_H_ */
