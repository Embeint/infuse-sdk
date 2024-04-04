/**
 * @file
 * @brief Internal ePacket interface helpers
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef EMBEINT_SDK_SUBSYS_EPACKET_INTERFACES_EPACKET_INTERNAL_H_
#define EMBEINT_SDK_SUBSYS_EPACKET_INTERFACES_EPACKET_INTERNAL_H_

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

#ifdef __cplusplus
}
#endif

#endif /* EMBEINT_SDK_SUBSYS_EPACKET_INTERFACES_EPACKET_INTERNAL_H_ */
