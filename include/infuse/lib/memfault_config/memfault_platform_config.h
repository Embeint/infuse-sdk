/**
 * @file
 * @brief Infuse-IoT Memfault configuration
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifdef CONFIG_MEMFAULT_INFUSE_METRICS_BATTERY
#define MEMFAULT_METRICS_BATTERY_ENABLE              1
#define MEMFAULT_METRICS_BATTERY_SOC_PCT_SCALE_VALUE 1
#endif

/* Infuse-IoT does not generate trace events from ISRs */
#define MEMFAULT_TRACE_EVENT_WITH_LOG_FROM_ISR_ENABLED 0
/* Larger event log size required for secure fault info */
#define MEMFAULT_TRACE_EVENT_MAX_LOG_LEN               100
