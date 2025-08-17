/**
 * @file
 * @brief Infuse-IoT vendor specific commands
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_BLUETOOTH_INFUSE_HCI_VS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_BLUETOOTH_INFUSE_HCI_VS_H_

#include <zephyr/bluetooth/hci_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief infuse_hci_vs API
 * @defgroup infuse_hci_vs_apis infuse_hci_vs APIs
 * @{
 */

enum infuse_hci_opcode_vs {
	INFUSE_HCI_OPCODE_CMD_VS_EPACKET = BT_OP(BT_OGF_VS, 0x00E0),
};

/** @brief Infuse-IoT ePacket command parameter(s). */
typedef struct __PACKED __ALIGN(1) {
	/** @brief ePacket payload type */
	uint8_t type;
	/** @brief ePacket payload flags */
	uint16_t flags;
	/** @brief Sequence counter */
	uint16_t sequence;
	/* ePacket data payload */
	uint8_t data[];
} infuse_hci_cmd_vs_epacket_t;

enum infuse_hci_event_vs {
	INFUSE_HCI_EVT_VS_EPACKET = 0xE0,
};

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_BLUETOOTH_INFUSE_HCI_VS_H_ */
