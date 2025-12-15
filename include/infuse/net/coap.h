/**
 * @file
 * @brief Infuse-IoT COAP download helpers
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_NET_COAP_H_
#define INFUSE_SDK_INCLUDE_INFUSE_NET_COAP_H_

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Infuse COAP API
 * @defgroup infuse_coap_apis Infuse COAP APIs
 * @{
 */

/**
 * @brief COAP data download callback
 *
 * @param offset Offset of data payload from start of data
 * @param data Data payload
 * @param data_len Length of payload
 * @param user_context Arbitrary pointer from user
 *
 * @retval 0 Continue downloading more data
 * @retval -errno Terminate download process
 */
typedef int (*infuse_coap_data_cb)(uint32_t offset, const uint8_t *data, uint16_t data_len,
				   void *user_context);

/**
 * @brief Download a file over COAP from an existing socket
 *
 * @param socket Socket already connected to remote server
 * @param resource Resource path URI, for example "path/to/resource"
 * @param data_cb Callback run on each data chunk received
 * @param user_context Arbitrary user context for @a data_cb
 * @param working_mem Memory buffer for sending/receiving packets with
 * @param working_size Size of @a working_mem in bytes
 * @param block_size COAP block size to use (in bytes, 0 == auto)
 * @param timeout_ms Timeout waiting for each response from server
 *
 * @retval >=0 bytes downloaded on success
 * @retval <0 error code on failure
 */
int infuse_coap_download(int socket, const char *resource, infuse_coap_data_cb data_cb,
			 void *user_context, uint8_t *working_mem, size_t working_size,
			 uint16_t block_size, int timeout_ms);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_NET_COAP_H_ */
