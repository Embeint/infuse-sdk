/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>

#include <infuse/lib/lte_modem_monitor.h>

#include <infuse/net/dns.h>

LOG_MODULE_REGISTER(infuse_dns, LOG_LEVEL_INF);

#ifdef CONFIG_DNS_RESOLVER
K_SEM_DEFINE(dns_ctx, CONFIG_DNS_NUM_CONCUR_QUERIES, CONFIG_DNS_NUM_CONCUR_QUERIES);
#endif /* CONFIG_DNS_RESOLVER */

static void dns_result_display(struct sockaddr *addr, const char *host, uint16_t dns_id)
{
	char addr_str[INET6_ADDRSTRLEN];

	zsock_inet_ntop(addr->sa_family, &net_sin(addr)->sin_addr, addr_str, sizeof(addr_str));
	if (host != NULL) {
		LOG_INF("%s -> %s", host, addr_str);
	} else {
		LOG_INF("%04X -> %s", dns_id, addr_str);
	}
}

static void sockaddr_port_assign(struct sockaddr *addr, uint16_t port)
{
#ifdef CONFIG_NET_IPV4
	if (addr->sa_family == AF_INET) {
		struct sockaddr_in *ipv4 = (struct sockaddr_in *)addr;

		ipv4->sin_port = htons(port);
	}
#endif /* CONFIG_NET_IPV4 */
#ifdef CONFIG_NET_IPV6
	if (addr->sa_family == AF_INET6) {
		struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)addr;

		ipv6->sin6_port = htons(port);
	}
#endif /* CONFIG_NET_IPV6 */
}

int infuse_sync_dns(const char *host, uint16_t port, int family, int socktype,
		    struct sockaddr *addr, socklen_t *addrlen)
{
	struct zsock_addrinfo hints = {
		.ai_family = family,
		.ai_socktype = socktype,
	};
	struct zsock_addrinfo *res = NULL;
	int rc;

#ifdef CONFIG_INFUSE_NRF_MODEM_MONITOR
	if (!lte_modem_monitor_is_at_safe()) {
		/* Modem may be in a temporarily unresponsive state */
		return -EAGAIN;
	}
#endif /* CONFIG_INFUSE_NRF_MODEM_MONITOR */

#ifdef CONFIG_DNS_RESOLVER
	/* Take a context */
	(void)k_sem_take(&dns_ctx, K_FOREVER);
#endif /* CONFIG_DNS_RESOLVER */

	/* Perform DNS query */
	rc = zsock_getaddrinfo(host, NULL, &hints, &res);
	if (rc < 0) {
		LOG_WRN("%s -> Lookup failed (%d, %d)", host, rc, errno);
#ifdef CONFIG_DNS_RESOLVER
		k_sem_give(&dns_ctx);
#endif /* CONFIG_DNS_RESOLVER */
		return rc;
	}
	/* Store the first result */
	*addr = *res->ai_addr;
	*addrlen = res->ai_addrlen;
	/* Free the allocated memory */
	zsock_freeaddrinfo(res);
#ifdef CONFIG_DNS_RESOLVER
	k_sem_give(&dns_ctx);
#endif /* CONFIG_DNS_RESOLVER */

	/* Populate the port */
	sockaddr_port_assign(addr, port);

	/* Display DNS result */
	dns_result_display(addr, host, 0);
	return 0;
}

#ifdef CONFIG_INFUSE_DNS_ASYNC

static void dns_result_cb(enum dns_resolve_status status, struct dns_addrinfo *info,
			  void *user_data)
{
	struct infuse_async_dns_context *context = user_data;
	int rc;

	switch (status) {
	case DNS_EAI_NODATA:
		LOG_WRN("%04X -> Lookup failed (%d, %d)", context->_dns_id, DNS_EAI_NODATA,
			DNS_EAI_NODATA);
		rc = -EINVAL;
		goto done;
	case DNS_EAI_ALLDONE:
		LOG_DBG("DNS resolving finished");
		rc = INFUSE_ASYNC_DNS_COMPLETE;
		goto done;
	case DNS_EAI_INPROGRESS:
		LOG_DBG("DNS resolving in progress");
		rc = INFUSE_ASYNC_DNS_RESULT;
		break;
	default:
		LOG_WRN("%04X -> DNS resolving error (%d)", context->_dns_id, status);
		rc = -EIO;
		goto done;
	}

	if (!info) {
		return;
	}

	/* Display DNS result */
	dns_result_display(&info->ai_addr, NULL, context->_dns_id);

	context->cb(INFUSE_ASYNC_DNS_RESULT, &info->ai_addr, info->ai_addrlen, context);
	return;
done:
	context->cb(rc, NULL, 0, context);
	/* Querying complete, release the context */
	k_sem_give(&dns_ctx);
}

int infuse_async_dns(const char *host, int family, struct infuse_async_dns_context *context,
		     int32_t timeout)
{
	enum dns_query_type query_type;
	int rc;

	if (family == AF_INET) {
		query_type = DNS_QUERY_TYPE_A;
	} else if (family == AF_INET6) {
		query_type = DNS_QUERY_TYPE_AAAA;
	} else {
		return -EINVAL;
	}

	/* Take a context */
	(void)k_sem_take(&dns_ctx, K_FOREVER);

	/* Start the DNS query process */
	rc = dns_get_addr_info(host, query_type, &context->_dns_id, dns_result_cb, context,
			       timeout);
	if (rc < 0) {
		LOG_ERR("Failed to start DNS query for '%s'", host);
		/* Release the context on error */
		k_sem_give(&dns_ctx);
	} else {
		LOG_INF("Started DNS query for '%s' (ID %04X)", host, context->_dns_id);
	}
	return rc;
}

#endif /* CONFIG_INFUSE_DNS_ASYNC */
