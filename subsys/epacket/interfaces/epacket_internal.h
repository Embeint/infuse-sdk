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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Claim scratch space for encryption
 *
 * @return struct net_buf* scratch space
 */
struct net_buf *epacket_encryption_scratch(void);

#ifdef __cplusplus
}
#endif

#endif /* EMBEINT_SDK_SUBSYS_EPACKET_INTERFACES_EPACKET_INTERNAL_H_ */
