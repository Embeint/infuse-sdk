/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include "common_net_query.h"

#if defined(CONFIG_NET_NATIVE_IPV4)
static void ipv4_callback(struct net_if *iface, struct net_if_addr *addr, void *user_data)
{
	struct rpc_struct_ipv4_address *rpc_addr = user_data;

	memcpy(rpc_addr, addr->address.in_addr.s4_addr, sizeof(*rpc_addr));
}
#endif

#if defined(CONFIG_NET_NATIVE_IPV6)
static void ipv6_callback(struct net_if *iface, struct net_if_addr *addr, void *user_data)
{
	struct rpc_struct_ipv6_address *rpc_addr = user_data;

	memcpy(rpc_addr, addr->address.in6_addr.s6_addr, sizeof(*rpc_addr));
}
#endif

void rpc_common_net_query(struct net_if *iface, struct rpc_struct_network_state *out)
{
	out->state = net_if_oper_state(iface);
	out->if_flags = iface->if_dev->flags[0];
	if (iface->if_dev->l2->get_flags != NULL) {
		out->l2_flags = iface->if_dev->l2->get_flags(iface);
	}
	out->mtu = net_if_get_mtu(iface);

	if (out->state != NET_IF_OPER_UP) {
		/* Extra fields are invalid */
		return;
	}

#if defined(CONFIG_NET_NATIVE_IPV4)
	net_if_ipv4_addr_foreach(iface, ipv4_callback, &out->ipv4.addr);
#endif
#if defined(CONFIG_NET_NATIVE_IPV6)
	net_if_ipv6_addr_foreach(iface, ipv6_callback, &out->ipv6.addr);
#endif
}
