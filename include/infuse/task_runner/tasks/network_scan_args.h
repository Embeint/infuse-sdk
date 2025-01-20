/**
 * @file
 * @brief Network scan task arguments
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_NETWORK_SCAN_ARGS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_NETWORK_SCAN_ARGS_H_

#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	/** @ref TDF_LTE_TAC_CELLS */
	TASK_NETWORK_SCAN_LOG_LTE_CELLS = BIT(0),
};

enum {
	TASK_NETWORK_SCAN_FLAGS_LTE_CELLS = BIT(0),
};

/** @brief Network scan task arguments */
struct task_network_scan_args {
	/** Meta operation flags */
	uint8_t flags;
	/** LTE Cell scanning arguments */
	struct {
		/**
		 * Number of LTE cells we want to report.
		 * Searching expands to more energy intensive methods until this number is found.
		 * Value is the summation of the current serving cell, neighbour cells and GCI
		 * cells.
		 */
		uint8_t desired_cells;
	} lte;

} __packed;

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TASK_RUNNER_TASKS_NETWORK_SCAN_ARGS_H_ */
