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
	/** @ref TDF_WIFI_AP_INFO */
	TASK_NETWORK_SCAN_LOG_WIFI_AP = BIT(1),
	/** @ref TDF_NETWORK_SCAN_COUNT */
	TASK_NETWORK_SCAN_LOG_COUNT = BIT(2),
};

enum {
	/** Scan nearby LTE cells */
	TASK_NETWORK_SCAN_FLAGS_LTE_CELLS = BIT(0),
	/** Scan nearby Wi-Fi access points */
	TASK_NETWORK_SCAN_FLAGS_WIFI_CELLS = BIT(1),
	/** Skip LTE scan if @a desired_aps Wi-Fi access points found */
	TASK_NETWORK_SCAN_FLAGS_SKIP_LTE_IF_WIFI_GOOD = BIT(7),
};

enum {
	/**
	 * A single access point can broadcast multiple networks simultaneously.
	 * This can usually be detected through the BSSID, which is the same for all networks
	 * being broadcast except the least significant nibble (4 bits).
	 * Reporting multiple networks from the same AP is generally not useful for localisation
	 * purposes. When set, multiple networks from the same AP will be reported anyway.
	 */
	TASK_NETWORK_SCAN_WIFI_FLAGS_INCLUDE_DUPLICATES = BIT(0),
	/**
	 * Reporting networks that use a locally administered BSSID is generally not useful for
	 * localisation purposes. When set, locally administered BSSIDs' are reported anyway.
	 */
	TASK_NETWORK_SCAN_WIFI_FLAGS_INCLUDE_LOCALLY_ADMINISTERED = BIT(0),
	/** If enabled, scan Wi-Fi channels over multiple calls in order of most to least
	 * common. Scanning terminates as soon as @a desired_aps is reached.
	 */
	TASK_NETWORK_SCAN_WIFI_FLAGS_SCAN_PROGRESSIVE = BIT(1),
	/** Active scanning, default is passive */
	TASK_NETWORK_SCAN_WIFI_FLAGS_SCAN_ACTIVE = BIT(2),
};

/** @brief Network scan task arguments */
struct task_network_scan_args {
	/** Meta operation flags */
	uint8_t flags;
	/** Wi-Fi AP scanning arguments */
	struct {
		/** Wi-Fi scanning flags */
		uint8_t flags;
		/** Number of unique access-points we want */
		uint8_t desired_aps;
		/** Maximum number of access-points to report */
		uint8_t max_aps;
	} wifi;
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
