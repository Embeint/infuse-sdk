/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

/* KV_KEY_Z_DEVSTATE is a reflected key, so the device state array written by
 * common_boot.c must fit in the KV store reflection size limit.
 */
ASSERT((__device_states_end - __device_states_start) <= CONFIG_KV_STORE_REFLECT_MAX_SIZE,
       "KV_KEY_Z_DEVSTATE exceeds CONFIG_KV_STORE_REFLECT_MAX_SIZE, increase the limit")
