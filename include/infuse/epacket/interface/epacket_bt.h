/**
 * @file
 * @brief ePacket defines for Bluetooth
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_BT_H_
#define INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_BT_H_

#include <zephyr/bluetooth/uuid.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief epacket_bt API
 * @defgroup epacket_bt_apis epacket_bt APIs
 * @{
 */

/* Infuse-IoT official Bluetooth SIG 16bit service UUID */
#define INFUSE_BT_SERVICE_UUID_VAL 0xFC74

#define INFUSE_SERVICE_UUID BT_UUID_DECLARE_16(INFUSE_BT_SERVICE_UUID_VAL)
#define INFUSE_SERVICE_UUID_COMMANDS                                                               \
	BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0xDC0B71B7, INFUSE_BT_SERVICE_UUID_VAL,             \
					       INFUSE_BT_SERVICE_UUID_VAL, 0xAA01,                 \
					       0x8ABA434A893D))
#define INFUSE_SERVICE_UUID_DATA                                                                   \
	BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0xDC0B71B7, INFUSE_BT_SERVICE_UUID_VAL,             \
					       INFUSE_BT_SERVICE_UUID_VAL, 0xAA02,                 \
					       0x8ABA434A893D))

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_BT_H_ */
