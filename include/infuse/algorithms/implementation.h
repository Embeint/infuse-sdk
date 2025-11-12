/**
 * @file
 * @brief API that algorithms must implement
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_ALGORITHM_RUNNER_LLEXT_IMPLEMENTATION_H_
#define INFUSE_SDK_INCLUDE_INFUSE_ALGORITHM_RUNNER_LLEXT_IMPLEMENTATION_H_

#include <zephyr/llext/symbol.h>

#include <infuse/algorithms/dependencies.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Infuse-IoT Algorithm implementation API
 * @defgroup infuse_algorithm_llext_implementation Infuse-IoT Algorithm implementation API
 * @{
 */

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
 */
typedef void (*algorithm_run_fn)(const struct zbus_channel *chan);

struct algorithm_common_config {
	/* Unique algorithm identifier */
	uint32_t algorithm_id;
	/* Primary channel that triggers algorithm run */
	uint32_t zbus_channel;
	/* Function that implements the algorithm */
	algorithm_run_fn fn;
} __packed;

/**
 * @brief Export algorithm implementation
 *
 * @param symbol Instance of @ref algorithm_common_config to export
 */
#define ALGORITHM_EXPORT(symbol) EXPORT_GROUP_SYMBOL_NAMED(INFUSE_ALG, symbol, algorithm_config);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_ALGORITHM_RUNNER_LLEXT_IMPLEMENTATION_H_ */
