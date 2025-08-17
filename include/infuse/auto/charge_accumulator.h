/**
 * @file
 * @brief Automatically accumulate battery charge measurements
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_AUTO_CHARGE_ACCUMULATOR_H_
#define INFUSE_SDK_INCLUDE_INFUSE_AUTO_CHARGE_ACCUMULATOR_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief charge_accumulator API
 * @defgroup charge_accumulator_apis charge_accumulator APIs
 * @{
 */

/**
 * @brief Get the amount of charge that has entered/exited the battery since the last call
 *
 * @note Each call to this function resets the tracked charge
 *
 * @param num_measurements Number of measurements since the last call (optional)
 *
 * @return int64_t Charge in microamp-seconds (+ve entering battery, -ve exiting battery)
 */
int64_t auto_charge_accumulator_query(uint32_t *num_measurements);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_AUTO_CHARGE_ACCUMULATOR_H_ */
