/**
 * @file
 * @brief ePacket defines for Bluetooth
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_BT_H_
#define INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_BT_H_

#include <zephyr/bluetooth/uuid.h>
#include <zephyr/toolchain.h>

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
#define INFUSE_SERVICE_UUID_COMMAND_VAL                                                            \
	BT_UUID_128_ENCODE(0xDC0B71B7, INFUSE_BT_SERVICE_UUID_VAL, INFUSE_BT_SERVICE_UUID_VAL,     \
			   0xAA01, 0x8ABA434A893D)
#define INFUSE_SERVICE_UUID_COMMAND BT_UUID_DECLARE_128(INFUSE_SERVICE_UUID_COMMAND_VAL)
#define INFUSE_SERVICE_UUID_DATA_VAL                                                               \
	BT_UUID_128_ENCODE(0xDC0B71B7, INFUSE_BT_SERVICE_UUID_VAL, INFUSE_BT_SERVICE_UUID_VAL,     \
			   0xAA02, 0x8ABA434A893D)
#define INFUSE_SERVICE_UUID_DATA BT_UUID_DECLARE_128(INFUSE_SERVICE_UUID_DATA_VAL)
#define INFUSE_SERVICE_UUID_LOGGING_VAL                                                            \
	BT_UUID_128_ENCODE(0xDC0B71B7, INFUSE_BT_SERVICE_UUID_VAL, INFUSE_BT_SERVICE_UUID_VAL,     \
			   0xAA03, 0x8ABA434A893D)
#define INFUSE_SERVICE_UUID_LOGGING BT_UUID_DECLARE_128(INFUSE_SERVICE_UUID_LOGGING_VAL)

/**
 * @brief Response to read requests on both the Command and Data characteristics
 *
 * Contains public security credentials for communicating with device.
 */
struct epacket_read_response {
	/* Cloud public ECC key */
	uint8_t cloud_public_key[32];
	/* Device public ECC key */
	uint8_t device_public_key[32];
	/* Current network ID */
	uint32_t network_id;
} __packed;

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_BT_H_ */
