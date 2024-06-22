/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include "common_net_query.h"

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
	struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;

	if (ipv4 != NULL) {
		memcpy(out->ipv4.addr, ipv4->unicast[0].address.in_addr.s4_addr, 4);
	}
#endif
#if defined(CONFIG_NET_NATIVE_IPV6)
	struct net_if_ipv6 *ipv6 = iface->config.ip.ipv6;

	if (ipv6 != NULL) {
		memcpy(out->ipv6.addr, ipv6->unicast[0].address.in6_addr.s6_addr, 16);
	}
#endif
}
