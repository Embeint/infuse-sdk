/**
 * @file
 * @brief LoRa modem validation
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_LORA_H_
#define INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_LORA_H_

#include <zephyr/device.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup validation_lora LoRa validation API
 * @{
 */

enum {
	/** Validate that sensor powers up */
	VALIDATION_LORA_POWER_UP = 0,
	/** Transmit a packet */
	VALIDATION_LORA_TX = BIT(0),
	/** Receive a packet */
	VALIDATION_LORA_RX = BIT(1),
	/** Perform channel activity detection */
	VALIDATION_LORA_CAD = BIT(2),
};

/**
 * @brief Validate the behaviour of a LoRa modem
 *
 * @param dev LoRa device
 * @param flags Validation tests to run
 *
 * @retval 0 On success
 * @retval -errno On failure
 */
int infuse_validation_lora(const struct device *dev, uint8_t flags);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_LORA_H_ */
