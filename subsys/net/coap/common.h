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

/* Overestimate of the CoAP header overhead to receive N bytes of payload.
 * Includes IPv4 UDP header.
 */
#define COAP_RSP_OVERHEAD 64

/**
 * @brief Determine where the URI segments are for a resource string
 *
 * @param resource Path to resource, e.g. seg1/seg2/seg3
 * @param component_starts Location to store start of each segment
 * @param array_len Size of @a component_starts
 *
 * @retval >0 Number of array elements used
 * @retval -EINVAL Resource contains too many segments
 */
int ic_resource_path_split(const char *resource, uint8_t *component_starts, uint8_t array_len);

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
			    uint8_t *component_starts, uint8_t num_components);

/**
 * @brief Convert a working memory size to a CoAP block size
 *
 * @param working_size Amount of working memory available
 * @param block_size Requested CoAP block size in bytes (0 == auto)
 * @param used_size Output CoAP block size
 *
 * @retval 0 On success
 * @retval -EINVAL Invalid @a block_size
 * @retval -ENOMEM Insufficient working memory
 */
int ic_get_block_size(size_t working_size, uint16_t block_size, enum coap_block_size *used_size);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_SUBSYS_NET_COAP_COMMON_H_ */
