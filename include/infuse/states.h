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

#include <zephyr/sys/atomic.h>
#include <zephyr/sys/slist.h>

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
	/* Device is stationary (not moving) */
	INFUSE_STATE_DEVICE_STATIONARY = 4,
	/* Device is currently sending high-priority data to the cloud */
	INFUSE_STATE_HIGH_PRIORITY_UPLINK = 5,
	/* Device started moving */
	INFUSE_STATE_DEVICE_STARTED_MOVING = 6,
	/* Device stopped moving */
	INFUSE_STATE_DEVICE_STOPPED_MOVING = 7,
	/* Start of application-specific state range */
	INFUSE_STATES_APP_START = 128,
	INFUSE_STATES_END = UINT8_MAX
};

/* Required size of atomic array for Infuse-IoT application states */
#define INFUSE_STATES_ARRAY_SIZE ATOMIC_BITMAP_SIZE(INFUSE_STATES_END + 1)

/**
 * @brief Define a variable that can hold all Infuse-IoT application states
 */
#define INFUSE_STATES_ARRAY(name) atomic_t name[INFUSE_STATES_ARRAY_SIZE]

/** @brief Infuse-IoT application state callback structure. */
struct infuse_state_cb {
	/**
	 * @brief Application state has been set.
	 *
	 * @param state State that has been set
	 * @param already State was already set
	 * @param timeout Timeout for the state (0 for indefinite)
	 * @param user_ctx User context pointer
	 */
	void (*state_set)(enum infuse_state state, bool already, uint16_t timeout, void *user_ctx);

	/**
	 * @brief Application state has been cleared.
	 *
	 * @param state State that has been cleared
	 * @param user_ctx User context pointer
	 */
	void (*state_cleared)(enum infuse_state state, void *user_ctx);

	/* User provided context pointer */
	void *user_ctx;

	sys_snode_t node;
};

/**
 * @brief Register to be notified of state update events
 *
 * @param cb Callback struct to register
 */
void infuse_state_register_callback(struct infuse_state_cb *cb);

/**
 * @brief Unregister previously registered callback structure
 *
 * @param cb Callback struct to unregister
 *
 * @retval true When callback structure unregistered
 * @retval false When structure was not previously registered
 */
bool infuse_state_unregister_callback(struct infuse_state_cb *cb);

/**
 * @brief Set an application state
 *
 * Application state will remain set until @ref infuse_state_clear is called.
 * Any pending timeouts from @ref infuse_state_set_timeout will be cancelled.
 *
 * @param state State to set
 *
 * @return true if the state was already set, false if it wasn't.
 */
bool infuse_state_set(enum infuse_state state);

/**
 * @brief Set an application state that times out after a duration
 *
 * Calling this function multiple times will reschedule the timeout each time.
 * If the state was previously set without a timeout via @ref infuse_state_set,
 * a timeout will be added.
 *
 * @param state State to set
 * @param timeout Seconds that state should be set for
 *
 * @return true if the state was already set, false if it wasn't.
 */
bool infuse_state_set_timeout(enum infuse_state state, uint16_t timeout);

/**
 * @brief Get the timeout associated with a state
 *
 * @param state State to query timeout of
 *
 * @retval -EINVAL is state is not set
 * @retval 0 if state is set but has no timeout
 * @retval timeout seconds until the state is cleared otherwise
 */
int infuse_state_get_timeout(enum infuse_state state);

/**
 * @brief Clear an application state
 *
 * @param state State to clear
 *
 * @return false if the bit was already cleared, true if it wasn't.
 */
bool infuse_state_clear(enum infuse_state state);

/**
 * @brief Set an application state to a specific value
 *
 * @param state State to set
 * @param val Value to set to
 *
 * @return true if the state was previously set, false if it wasn't.
 */
static inline bool infuse_state_set_to(enum infuse_state state, bool val)
{
	if (val) {
		return infuse_state_set(state);
	} else {
		return infuse_state_clear(state);
	}
}

/**
 * @brief Get an application state
 *
 * @param state State to query
 * @retval true Application state is set
 * @retval false Application state is not set
 */
bool infuse_state_get(enum infuse_state state);

/**
 * @brief Get a snapshot of the current application states
 *
 * @param snapshot Memory to store snapshot in
 */
void infuse_states_snapshot(atomic_t snapshot[INFUSE_STATES_ARRAY_SIZE]);

/**
 * @brief Run one tick of the state timeouts.
 *
 * The requirement to provide the snapshotted state is to prevent situations where a state is set
 * just before this function is called, but after the consumer of the states has run. The typical
 * concrete example of this is the task runner. This feature ensures that for a timeout of N, the
 * state is set for N iterations of the task runner evaluation.
 *
 * @note This function must be run once and only once per second for correct operation
 *
 * @param snapshot States that were present at evaluation time, from @ref infuse_states_snapshot
 */
void infuse_states_tick(atomic_t snapshot[INFUSE_STATES_ARRAY_SIZE]);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_STATES_H_ */
