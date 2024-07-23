/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>

#include <infuse/net/dns.h>

LOG_MODULE_REGISTER(infuse_dns, LOG_LEVEL_INF);

K_SEM_DEFINE(dns_ctx, CONFIG_DNS_NUM_CONCUR_QUERIES, CONFIG_DNS_NUM_CONCUR_QUERIES);

int infuse_sync_dns(const char *host, uint16_t port, int family, int socktype,
		    struct sockaddr *addr, socklen_t *addrlen)
{
	struct zsock_addrinfo hints = {
		.ai_family = family,
		.ai_socktype = socktype,
	};
	struct zsock_addrinfo *res = NULL;
	int rc;

	/* Take a context */
	(void)k_sem_take(&dns_ctx, K_FOREVER);

	/* Perform DNS query */
	rc = zsock_getaddrinfo(host, NULL, &hints, &res);
	if (rc < 0) {
		LOG_WRN("%s -> Lookup failed", host);
		k_sem_give(&dns_ctx);
		return rc;
	}
	/* Store the first result */
	*addr = *res->ai_addr;
	*addrlen = res->ai_addrlen;
	/* Free the allocated memory */
	zsock_freeaddrinfo(res);
	k_sem_give(&dns_ctx);

#ifdef CONFIG_NET_IPV4
	if (addr->sa_family == AF_INET) {
		struct sockaddr_in *ipv4 = (struct sockaddr_in *)addr;
		uint8_t *b = ipv4->sin_addr.s4_addr;

		ipv4->sin_port = htons(port);
		LOG_INF("%s -> %d.%d.%d.%d:%d", host, b[0], b[1], b[2], b[3], port);
	}
#endif /* CONFIG_NET_IPV4 */
#ifdef CONFIG_NET_IPV6
	if (addr->sa_family == AF_INET6) {
		struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)addr;

		ipv6->sin6_port = htons(port);
		LOG_INF("%s -> IPv6:%d", host, port);
	}
#endif /* CONFIG_NET_IPV6 */
	return 0;
}
