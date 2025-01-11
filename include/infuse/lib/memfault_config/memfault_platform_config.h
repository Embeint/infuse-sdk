/**
 * @file
 * @brief Infuse-IoT Memfault configuration
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifdef CONFIG_MEMFAULT_INFUSE_METRICS_BATTERY
#define MEMFAULT_METRICS_BATTERY_ENABLE              1
#define MEMFAULT_METRICS_BATTERY_SOC_PCT_SCALE_VALUE 1
#endif
