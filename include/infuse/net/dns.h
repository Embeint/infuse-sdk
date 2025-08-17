/**
 * @file
 * @brief Infuse DNS helpers
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_NET_DNS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_NET_DNS_H_

#include <stdint.h>

#include <zephyr/net/net_ip.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Infuse DNS API
 * @defgroup infuse_dns_apis dns Infuse DNS APIs
 * @{
 */

/**
 * @brief Perform a DNS query for a host
 *
 * @note Also populates port number
 *
 * @param host Host to lookup
 * @param port Port number of host
 * @param family Protocol family hint
 * @param socktype Socket type hint
 * @param addr Output storage for host address
 * @param addrlen Output length of address
 *
 * @retval 0 on success
 * @retval -errno on failure
 */
int infuse_sync_dns(const char *host, uint16_t port, int family, int socktype,
		    struct sockaddr *addr, socklen_t *addrlen);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_NET_DNS_H_ */
