/**
 * @file
 * @brief
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 */

#ifndef INFUSE_SDK_SUBSYS_NET_COAP_COMMON_H_
#define INFUSE_SDK_SUBSYS_NET_COAP_COMMON_H_

#include <stdint.h>
#include <stdlib.h>

#include <zephyr/net/coap.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Overheads associated with receiving a N byte CoAP block over DTLS 1.2.
 * Assuming the PSK_AES128_SHA256 cipher.
 *
 *             IPv4 Header: 20 bytes
 *              UDP Header: 8 bytes
 *  DTLS 1.2 Record Header: 13 bytes
 *         DTLS 1.2 Crypto: 64 bytes
 *             CoAP Header: 16 bytes for 4 byte tokens
 */
#define COAP_RSP_OVERHEAD (20 + 8 + 13 + 64 + 16)

/* Minimum working memory required on top of the block size.
 *
 * Received datagrams only contain the CoAP message, which is the payload plus
 * the CoAP header, token and options. This is the allowance for those fields.
 */
#define COAP_RSP_MIN_OVERHEAD 64

/* Maximum length of a single Uri-Path option (RFC 7252, section 5.10) */
#define COAP_URI_PATH_SEGMENT_MAX_LEN 255

/**
 * @brief Determine where the URI segments are for a resource string
 *
 * @param resource Path to resource, e.g. seg1/seg2/seg3
 * @param component_starts Location to store start of each segment, element 0 must be 0.
 *                         One more element than the number of segments is used.
 * @param array_len Size of @a component_starts
 *
 * @retval >0 Number of segments
 * @retval -EINVAL Resource contains too many segments, or a segment that is too long
 */
int ic_resource_path_split(const char *resource, uint16_t *component_starts, uint8_t array_len);

/**
 * @brief Append the resource segments to the CoAP request
 *
 * @param request CoAP request packet
 * @param resource Path to resource, e.g. seg1/seg2/seg3
 * @param component_starts Array populated by @ref resource_path_split
 * @param num_components Return value from @ref resource_path_split
 *
 * @retval 0 On success
 * @retval -errno Error code from @a coap_packet_append_option on failure
 */
int ic_resource_path_append(struct coap_packet *request, const char *resource,
			    const uint16_t *component_starts, uint8_t num_components);

/**
 * @brief Convert a working memory size to a CoAP block size
 *
 * @param working_size Amount of working memory available
 * @param block_size Requested CoAP block size in bytes (0 == auto)
 * @param used_size Output CoAP block size
 *
 * Explicit block sizes are only accepted if @a working_size can hold the block
 * plus @ref COAP_RSP_MIN_OVERHEAD bytes.
 *
 * @retval 0 On success
 * @retval -EINVAL Invalid @a block_size
 * @retval -ENOMEM Insufficient working memory
 */
int ic_get_block_size(size_t working_size, uint16_t block_size, enum coap_block_size *used_size);

/**
 * @brief Acknowledge a confirmable response from the server
 *
 * Confirmable (separate) responses must be acknowledged with an empty ACK,
 * otherwise the server retransmits them (RFC 7252, section 5.2.2).
 * No-op if @a response is not confirmable.
 *
 * @param socket Socket connected to the server
 * @param response Received response
 */
void ic_ack_if_confirmable(int socket, const struct coap_packet *response);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_SUBSYS_NET_COAP_COMMON_H_ */
