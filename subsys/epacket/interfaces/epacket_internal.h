/**
 * @file
 * @brief Internal ePacket interface helpers
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_SUBSYS_EPACKET_INTERFACES_EPACKET_INTERNAL_H_
#define INFUSE_SDK_SUBSYS_EPACKET_INTERFACES_EPACKET_INTERNAL_H_

#include <zephyr/net/buf.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Infuse-IoT official Bluetooth SIG 16bit service UUID */
#define INFUSE_BT_SERVICE_UUID_VAL 0xFC74

/**
 * @brief Common initialisation for all interfaces
 *
 * @param dev Interface
 */
void epacket_interface_common_init(const struct device *dev);

/**
 * @brief Notify parties of packet TX result
 *
 * @param dev ePacket interface
 * @param buf ePacket packet
 * @param result Result of the TX send
 */
void epacket_notify_tx_result(const struct device *dev, struct net_buf *buf, int result);

/**
 * @brief Claim scratch space for encryption
 *
 * @return struct net_buf* scratch space
 */
struct net_buf *epacket_encryption_scratch(void);

/**
 * @brief Handle raw received ePackets from interfaces
 *
 * @param buf ePacket that was received
 */
void epacket_raw_receive_handler(struct net_buf *buf);

/**
 * @brief Reconstruct serial packet from byte stream
 *
 * @param dev Serial device
 * @param buffer Byte stream buffer
 * @param len Length of byte stream
 * @param handler Function to call on recovered packet
 */
void epacket_serial_reconstruct(const struct device *dev, uint8_t *buffer, size_t len,
				void (*handler)(struct net_buf *));

/**
 * @brief Encrypt serial packet for transmission
 *
 * @param buf Packet to encrypt
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
int epacket_serial_encrypt(struct net_buf *buf);

/**
 * @brief Decrypt received serial packet
 *
 * @param buf Packet to decrypt
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
int epacket_serial_decrypt(struct net_buf *buf);

/**
 * @brief Initialise Bluetooth AD structures
 */
void epacket_bt_adv_ad_init(void);

/**
 * @brief Convert ePacket net_buf to Bluetooth AD structure
 *
 * @param buf ePacket buffer to convert
 * @param num Number of AD structures to advertise
 *
 * @returns Pointer to array of `struct bt_data` to advertise
 */
void *epacket_bt_adv_pkt_to_ad(struct net_buf *buf, size_t *num);

/**
 * @brief Check if Bluetooth advertising packet is an ePacket
 *
 * @note On success, the Bluetooth headers are removed from @a buf,
 *       leaving only the ePacket payload.
 *
 * @param adv_type Bluetooth advertising type
 * @param buf Serialised Bluetooth advertising buffer
 *
 * @return true Packet is an ePacket
 * @return false Packet is not an ePacket
 */
bool epacket_bt_adv_is_epacket(uint8_t adv_type, struct net_buf_simple *buf);

/**
 * @brief Encrypt Bluetooth advertising packet for transmission
 *
 * @param buf Packet to encrypt
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
int epacket_bt_adv_encrypt(struct net_buf *buf);

/**
 * @brief Decrypt received Bluetooth advertising packet
 *
 * @param buf Packet to decrypt
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
int epacket_bt_adv_decrypt(struct net_buf *buf);

/**
 * @brief Encrypt UDP packet for transmission
 *
 * @param buf Packet to encrypt
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
int epacket_udp_encrypt(struct net_buf *buf);

/**
 * @brief Decrypt received UDP packet
 *
 * @param buf Packet to decrypt
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
int epacket_udp_decrypt(struct net_buf *buf);

/**
 * @brief Decrypt received HCI packet
 *
 * @param buf Packet to decrypt
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
int epacket_hci_decrypt(struct net_buf *buf);

/**
 * @brief Decrypt received dummy packet
 *
 * @param buf Packet to decrypt
 *
 * @retval 0 on success
 */
int epacket_dummy_decrypt(struct net_buf *buf);

/**
 * @brief Common V0 packet encryption for transmission
 *
 * @param buf Packet to encrypt
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
int epacket_versioned_v0_encrypt(struct net_buf *buf, uint8_t interface_key);

/**
 * @brief Decrypt received common V0 packet
 *
 * @param buf Packet to decrypt
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
int epacket_versioned_v0_decrypt(struct net_buf *buf, uint8_t interface_key);

/**
 * @brief Common V0 packet encryption for transmission
 *
 * @param buf Packet to encrypt
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
int epacket_unversioned_v0_encrypt(struct net_buf *buf, uint8_t interface_key);

/**
 * @brief Decrypt received common V0 packet
 *
 * @param buf Packet to decrypt
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
int epacket_unversioned_v0_decrypt(struct net_buf *buf, uint8_t interface_key);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_SUBSYS_EPACKET_INTERFACES_EPACKET_INTERNAL_H_ */
