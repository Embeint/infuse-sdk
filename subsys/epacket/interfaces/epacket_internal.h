/**
 * @file
 * @brief Internal ePacket interface helpers
 * @copyright 2024 Embeint Pty Ltd
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

/**
 * @brief Claim scratch space for encryption
 *
 * @return struct net_buf* scratch space
 */
struct net_buf *epacket_encryption_scratch(void);

#define SERIAL_SYNC_A 0xD5
#define SERIAL_SYNC_B 0xCA

/**
 * @brief Reconstruct serial packet from byte stream
 *
 * @param dev Serial device
 * @param buffer Byte stream buffer
 * @param len Length of byte stream
 * @param handler Function to call on recovered packet
 */
void epacket_serial_reconstruct(const struct device *dev, uint8_t *buffer, size_t len,
				void (*handler)(struct epacket_receive_metadata *, struct net_buf *));

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
 * @param sequence Sequence number of packet
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
int epacket_serial_decrypt(struct net_buf *buf, uint16_t *sequence);

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
 * @param sequence Sequence number of packet
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
int epacket_udp_decrypt(struct net_buf *buf, uint16_t *sequence);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_SUBSYS_EPACKET_INTERFACES_EPACKET_INTERNAL_H_ */
