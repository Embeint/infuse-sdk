/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_NRF_MODEM_H_
#define INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_NRF_MODEM_H_

#include <zephyr/device.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup validation_nrf_modem nRF modem validation API
 * @{
 */

enum {
	/** Check modem firmware version */
	VALIDATION_NRF_MODEM_FW_VERSION = 0,
	/** Check SIM card found */
	VALIDATION_NRF_MODEM_SIM_CARD = BIT(0),
};

/**
 * @brief Validate the behaviour of LTE modem
 *
 * @param flags Validation tests to run
 *
 * @retval 0 On success
 * @retval -errno On failure
 */
int infuse_validation_nrf_modem(uint8_t flags);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_NRF_MODEM_H_ */
