/**
 * @file
 * @brief Infuse-IoT algorithm runner
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_ALGORITHM_RUNNER_RUNNER_H_
#define INFUSE_SDK_INCLUDE_INFUSE_ALGORITHM_RUNNER_RUNNER_H_

#include <stdbool.h>
#include <stdint.h>

#include <infuse/algorithms/implementation.h>
#include <infuse/fs/kv_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Algorithm runner API
 * @defgroup runner_apis Algorithm runner APIs
 * @{
 */

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
 * @param config Algorithm configuration to register
 */
void algorithm_runner_register(const struct algorithm_common_config *config);

/**
 * @brief Unregister an algorithm from the runner
 *
 * @param config Algorithm configuration to unregister
 *
 * @retval true Algorithm was found and unregistered
 * @retval false Algorithm was not registered with the runner
 */
bool algorithm_runner_unregister(const struct algorithm_common_config *config);

/**
 * @brief Log a single TDF as requested by algorithm configuration
 *
 * @param logging Algorithm logging configuration
 * @param tdf_mask Single TDF mask that corresponds to @a tdf_id
 * @param tdf_id TDF sensor ID
 * @param tdf_len Length of a single TDF
 * @param time Epoch time associated with the TDF. 0 for no timestamp.
 * @param data TDF data array
 */
void algorithm_runner_tdf_log(const struct kv_algorithm_logging *logging, uint8_t tdf_mask,
			      uint16_t tdf_id, uint8_t tdf_len, uint64_t time, const void *data);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_ALGORITHM_RUNNER_RUNNER_H_ */
