/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_BACKENDS_EPACKET_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_BACKENDS_EPACKET_H_

#include <stdint.h>

#include <zephyr/devicetree.h>

#include <infuse/epacket/interface.h>

#include "backend_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DATA_LOGGER_BACKEND_CONFIG_EPACKET(node, data_ptr)                                         \
	{                                                                                          \
		.api = &data_logger_epacket_api, .data = data_ptr,                                 \
		.backend = DEVICE_DT_GET(DT_PROP(node, epacket)), .logical_blocks = UINT32_MAX,    \
		.physical_blocks = UINT32_MAX, .erase_size = 0,                                    \
		.max_block_size = EPACKET_INTERFACE_MAX_PAYLOAD(DT_PROP(node, epacket)),           \
	}

extern const struct data_logger_backend_api data_logger_epacket_api;

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_BACKENDS_EPACKET_H_ */
