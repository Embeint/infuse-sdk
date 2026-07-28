/**
 * @file
 * @brief Control over LTE connectivity in response to events
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 * Force disable LTE connectivity when @ref INFUSE_STATE_APPLICATION_ACTIVE is not set.
 * When active, control the LTE state according to @ref auto_lte_control_retry and
 * @ref auto_lte_control_give_up.
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_AUTO_LTE_CONTROL_H_
#define INFUSE_SDK_INCLUDE_INFUSE_AUTO_LTE_CONTROL_H_

#include <stdbool.h>

#include <zephyr/net/net_if.h>

#include <infuse/states.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise LTE control policy.
 *
 * @note Must be called by application `main` function to initialise behaviour, once only.
 */
void auto_lte_control_init(void);

/**
 * @brief If the LTE modem has not yet registered to a network, give up
 */
void auto_lte_control_give_up(void);

/**
 * @brief If the LTE modem previously stopped searching for a network to register to, try again
 */
void auto_lte_control_retry(void);

#ifdef CONFIG_ZTEST

/**
 * @brief Cleanup the module in a test context to allow re-initialisation
 */
void auto_lte_control_test_cleanup(void);

#endif /* CONFIG_ZTEST */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_AUTO_LTE_CONTROL_H_ */
