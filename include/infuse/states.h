/**
 * @file
 * @brief Application state framework
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_STATES_H_
#define INFUSE_SDK_INCLUDE_INFUSE_STATES_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup states_apis Infuse-IoT application state APIs
 * @{
 */

/**
 * @brief Infuse-IoT application states
 *
 *   1 - 127: Infuse-IoT defined states
 * 128 - 255: Application specific states
 */
enum infuse_state {
	/* Device is about to reboot */
	INFUSE_STATE_REBOOTING = 1,
	/* Application is active according to KV-store values */
	INFUSE_STATE_APPLICATION_ACTIVE = 2,
	/* Application has a valid time source */
	INFUSE_STATE_TIME_KNOWN = 3,
	/* Start of application-specific state range */
	INFUSE_STATES_APP_START = 128,
	INFUSE_STATES_END = UINT8_MAX
};

/**
 * @brief Set an application state
 *
 * Application state will remain set until @ref infuse_state_clear is called.
 *
 * @param state State to set
 */
void infuse_state_set(enum infuse_state state);

/**
 * @brief Set an application state that times out after a duration
 *
 * @param state State to set
 * @param timeout Seconds that state should be set for
 */
void infuse_state_set_timeout(enum infuse_state state, uint16_t timeout);

/**
 * @brief Clear an application state
 *
 * @param state State to clear
 */
void infuse_state_clear(enum infuse_state state);

/**
 * @brief Get an application state
 *
 * @param state State to query
 * @retval true Application state is set
 * @retval false Application state is not set
 */
bool infuse_state_get(enum infuse_state state);

/**
 * @brief Run one tick of the state timeouts.
 *
 * @note This function must be run once and only once per second for correct operation
 */
void infuse_states_tick(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_STATES_H_ */
