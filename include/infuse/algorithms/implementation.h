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

struct infuse_algorithm;

/**
 * @brief Algorithm implementation
 *
 * @warning The algorithm implementation ***MUST*** release the channel reference via @a
 * zbus_chan_finish before exiting. This should be done as soon as processing of the channel data
 * has completed.
 *
 * @param chan Channel pointer corresponding to @a zbus_channel in
 * @ref infuse_algorithm. Value is NULL on the very first call to initialise data
 * structures.
 * @param common Pointer to common algorithm config
 * @param args Pointer to algorithm specific arguments
 */
typedef void (*algorithm_run_fn)(const struct zbus_channel *chan,
				 const struct infuse_algorithm *algorithm, const void *args);

struct infuse_algorithm {
	/* Unique algorithm identifier */
	uint32_t algorithm_id;
	/* Primary channel that triggers algorithm run */
	uint32_t zbus_channel;
	/* Algorithm arguments */
	void *arguments;
	/* Size of the arguments structure */
	uint16_t arguments_size;
	/* KV Store key holding @a arguments (If > 0) */
	uint16_t arguments_kv_key;
	/* Function that implements the algorithm */
	algorithm_run_fn fn;
};

/**
 * @brief Export algorithm implementation
 *
 * @param symbol Instance of @ref infuse_algorithm to export
 */
#define ALGORITHM_EXPORT(symbol) EXPORT_GROUP_SYMBOL_NAMED(INFUSE_ALG, symbol, algorithm_config);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_ALGORITHM_RUNNER_LLEXT_IMPLEMENTATION_H_ */
