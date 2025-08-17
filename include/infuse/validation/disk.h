/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_DISK_H_
#define INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_DISK_H_

#include <zephyr/device.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup validation_disk Disk validation API
 * @{
 */

enum {
	/** Validate that flash powers up */
	VALIDATION_DISK_POWER_UP = 0,
	/** Rigorous driver behavioural tests */
	VALIDATION_DISK_DRIVER = BIT(0),
	/** Read and write throughput */
	VALIDATION_DISK_THROUGHPUT = BIT(1),
	/** Perform a full disk erase */
	VALIDATION_DISK_ERASE = BIT(2),
};

/**
 * @brief Validate the behaviour of external disks
 *
 * @param disk Disk name
 * @param flags Validation tests to run
 *
 * @retval 0 On success
 * @retval -errno On failure
 */
int infuse_validation_disk(const char *disk, uint8_t flags);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_DISK_H_ */
