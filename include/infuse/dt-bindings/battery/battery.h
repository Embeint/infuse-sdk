/*
 * Copyright 2026 Embeint Inc
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief Voltage curve for NCR18650B lithium-ion (Li-ion) cell.
 *
 * Computed from: https://zenodo.org/records/8369275
 */
#define BATTERY_OCV_CURVE_LITHIUM_ION_NCR18650B                                                    \
	2599027 3351986 3480944 3558280 3613087 3677543 3780681 3867016 3951470 4062082 4188507

/**
 * @brief Approximate internal resistance of a NCR18650B cell, in micro-ohms
 *
 * Computed from: https://zenodo.org/records/8369275
 */
#define BATTERY_INTERNAL_RESISTANCE_NCR18650B 47000

/**
 * @brief Maximum internal resistance of a MIPCM-1SXP2.5AT10K PCM, in micro-ohms
 *
 * From: https://www.master-instruments.com.au/file/60741/1/Enepower-MIPCM-1SXP2.5AT10K.pdf
 */
#define BATTERY_INTERNAL_RESISTANCE_MI_PCM_1SXP25 50000
