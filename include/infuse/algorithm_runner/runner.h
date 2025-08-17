/**
 * @file
 * @brief Infuse-IoT algorithm runner
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_ALGORITHM_RUNNER_RUNNER_H_
#define INFUSE_SDK_INCLUDE_INFUSE_ALGORITHM_RUNNER_RUNNER_H_

#include <stdint.h>

#include <zephyr/toolchain.h>
#include <zephyr/sys/slist.h>
#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Algorithm runner API
 * @defgroup runner_apis Algorithm runner APIs
 * @{
 */

struct algorithm_runner_common_config {
	/* Unique algorithm identifier */
	uint32_t algorithm_id;
	/* Primary channel that triggers algorithm run */
	uint32_t zbus_channel;
	/* Required runtime state size */
	uint16_t state_size;
	/* Algorithm output logging */
	struct {
		/** TDF loggers to log to */
		uint8_t loggers;
		/** TDFs to log (bitmask defined by the activity) */
		uint8_t tdf_mask;
	} logging;
} __packed;

/**
 * @brief Algorithm implementation
 *
 * @warning The algorithm implementation ***MUST*** release the channel reference via @a
 * zbus_chan_finish before exiting. This should be done as soon as processing of the channel data
 * has completed.
 *
 * @param chan Channel pointer corresponding to @a zbus_channel in
 * @ref algorithm_runner_common_config. Value is NULL on the very first call to initialise data
 * structures.
 * @param config Pointer to the constant algorithm configuration
 * @param data Pointer to the mutable algorithm state
 */
typedef void (*algorithm_run_fn)(const struct zbus_channel *chan, const void *config, void *data);

struct algorithm_runner_algorithm {
	/* Function that implements the algorithm */
	algorithm_run_fn impl;
	/* Algorithm configuration */
	const struct algorithm_runner_common_config *config;
	/* Algorithm runtime state */
	void *runtime_state;
	/* Internal state: new data on channel */
	const struct zbus_channel *_changed;
	/* Internal state: list node object*/
	sys_snode_t _node;
};

/**
 * @brief Initialise the algorithm runner
 *
 * Can be called multiple times to clear the list of registered algorithms.
 */
void algorithm_runner_init(void);

/**
 * @brief Register an algorithm with the runner
 *
 * @note Registering the algorithm will immediately call the implementation with `chan == NULL`
 *       to provide an opportunity to initialise runtime state.
 *
 * @param algorithm Algorithm to register
 */
void algorithm_runner_register(struct algorithm_runner_algorithm *algorithm);

/**
 * @brief Unregister an algorithm from the runner
 *
 * @param algorithm Algorithm to unregister
 *
 * @retval true Algorithm was found and unregistered
 * @retval false Algorithm was not registered with the runner
 */
bool algorithm_runner_unregister(struct algorithm_runner_algorithm *algorithm);

/**
 * @brief Log a single TDF as requested by algorithm configuration
 *
 * @param config Common algorithm configuration
 * @param tdf_mask Single TDF mask that corresponds to @a tdf_id
 * @param tdf_id TDF sensor ID
 * @param tdf_len Length of a single TDF
 * @param time Epoch time associated with the TDF. 0 for no timestamp.
 * @param data TDF data array
 */
void algorithm_runner_tdf_log(const struct algorithm_runner_common_config *config, uint8_t tdf_mask,
			      uint16_t tdf_id, uint8_t tdf_len, uint64_t time, const void *data);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_ALGORITHM_RUNNER_RUNNER_H_ */
