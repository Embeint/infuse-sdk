/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_NRF70_RADIO_TEST_H_
#define INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_NRF70_RADIO_TEST_H_

#include <zephyr/net/net_if.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup validation_nrf70_radio_test nRF70 validation API
 * @{
 */

enum {
	/** Perform the builtin XO tuning procedure */
	VALIDATION_NRF70_RADIO_TEST_XO_TUNE = BIT(0),
};

/**
 * @brief Validate the behaviour of a nRF70
 *
 * @param dev nRF70 WLAN device
 * @param flags Validation tests to run
 * @param channel Base WiFi channel for testing
 *
 * @retval 0 On success
 * @retval -errno On failure
 */
int infuse_validation_nrf70_radio_test(const struct device *dev, uint8_t flags, uint8_t channel);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_NRF70_RADIO_TEST_H_ */
