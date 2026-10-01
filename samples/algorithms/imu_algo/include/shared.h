/**
 * @file
 * @brief
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Aeyohan Furtado <aeyohan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_SAMPLES_ALGORITHMS_IMU_ALGO_INCLUDE_SHARED_H
#define INFUSE_SDK_SAMPLES_ALGORITHMS_IMU_ALGO_INCLUDE_SHARED_H

#ifdef __cplusplus
extern "C" {
#endif

void inference_led_pulse(bool start);

void broadcast_inference(uint8_t length, uint64_t start_ticks, const void *data);

#ifdef __cplusplus
}
#endif
#endif /* INFUSE_SDK_SAMPLES_ALGORITHMS_IMU_ALGO_INCLUDE_SHARED_H */
