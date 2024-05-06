/**
 * @file
 * @brief Core Infuse IoT platform types
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TYPES_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TYPES_H_

#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Infuse types
 * @defgroup infuse_types Infuse type
 * @{
 */

/* Core Infuse Data Types */
enum infuse_type {
	/* Echo of received data */
	INFUSE_ECHO = 0,
	/* Tagged Data Format */
	INFUSE_TDF = 1,
	/* 128 - 255 can be freely defined by customers */
	INFUSE_CUSTOMER_RANGE_START = 128,
} __packed;

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TYPES_H_ */
