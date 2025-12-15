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

/* One result for the DNS query (multiple results are possible) */
#define INFUSE_ASYNC_DNS_RESULT   0
/* DNS query has completed successfully */
#define INFUSE_ASYNC_DNS_COMPLETE 1

struct infuse_async_dns_context;

/**
 * @brief Callback when DNS results are received
 *
 * @param result @ref INFUSE_ASYNC_DNS_RESULT, @ref INFUSE_ASYNC_DNS_COMPLETE, or negative errno
 * @param addr For @ref INFUSE_ASYNC_DNS_RESULT, the address associated with the query
 * @param addrlen For @ref INFUSE_ASYNC_DNS_RESULT, the length of the address
 * @param cb_ctx @ref infuse_async_dns_context provided to @ref infuse_async_dns
 */
typedef void (*infuse_async_dns_cb)(int result, struct sockaddr *addr, socklen_t addrlen,
				    struct infuse_async_dns_context *cb_ctx);

/** Async query context for @ref infuse_async_dns */
struct infuse_async_dns_context {
	/* Callback to run on events */
	infuse_async_dns_cb cb;
	/* DNS query ID (Internal use) */
	uint16_t _dns_id;
	/* Arbitarary user context */
	void *user_data;
};

/**
 * @brief Perform an asynchronous DNS query for a host
 *
 * @param host Host to lookup
 * @param family Protocol family hint
 * @param context Context package for callbacks. Must remain valid until either
 *                @ref INFUSE_ASYNC_DNS_COMPLETE or error callback.
 * @param timeout_ms Timeout for query in milliseconds
 *
 * @retval 0 if query successfully started
 * @retval -errno other return value from dns_get_addr_info
 */
int infuse_async_dns(const char *host, int family, struct infuse_async_dns_context *context,
		     int32_t timeout_ms);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_NET_DNS_H_ */
