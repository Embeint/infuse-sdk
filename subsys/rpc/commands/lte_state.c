/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>

#include <infuse/rpc/commands.h>
#include <infuse/rpc/types.h>
#include <infuse/lib/lte_modem_monitor.h>

#include "common_net_query.h"

LOG_MODULE_DECLARE(rpc_server, CONFIG_INFUSE_RPC_LOG_LEVEL);

/* Validate our assumptions about V1 vs V2 response layout */
BUILD_ASSERT(sizeof(struct rpc_lte_state_v2_response) == sizeof(struct rpc_lte_state_response) + 2);
BUILD_ASSERT(offsetof(struct rpc_lte_state_v2_response, lte.as_rai) ==
	     sizeof(struct rpc_lte_state_response));
BUILD_ASSERT(offsetof(struct rpc_lte_state_v2_response, lte.cp_rai) ==
	     sizeof(struct rpc_lte_state_response) + 1);

static void lte_modem_lte_state(struct rpc_struct_lte_state_v2 *lte)
{
	struct lte_modem_network_state state;
	int16_t rsrp;
	int8_t rsrq;

	/* Generic network state */
	lte_modem_monitor_network_state(&state);

	lte->registration_state = state.nw_reg_status;
	lte->access_technology = state.lte_mode;
	lte->mcc = state.cell.mcc;
	lte->mnc = state.cell.mnc;
	lte->cell_id = state.cell.id;
	lte->tac = state.cell.tac;
	lte->tau = state.psm_cfg.tau;
	lte->earfcn = state.cell.earfcn;
	lte->band = state.band;
	lte->psm_active_time = state.psm_cfg.active_time;
	lte->edrx_interval = state.edrx_cfg.edrx;
	lte->edrx_paging_window = state.edrx_cfg.ptw;
	lte->as_rai = state.as_rai;
	lte->cp_rai = state.cp_rai;

	/* Current signal state */
	(void)lte_modem_monitor_signal_quality(&rsrp, &rsrq, false);
	lte->rsrp = rsrp;
	lte->rsrq = rsrq;
}

static struct net_buf *rpc_command_lte_state_common(struct net_buf *request, bool v2)
{
#if defined(CONFIG_NRF_MODEM_LIB)
	struct net_if *iface = net_if_get_first_by_type(&(NET_L2_GET_NAME(OFFLOADED_NETDEV)));
#elif defined(CONFIG_NET_L2_PPP)
	struct net_if *iface = net_if_get_first_by_type(&(NET_L2_GET_NAME(PPP)));
#else
#error Unknown LTE modem network interface
#endif
	/* V1 and V2 are the same, except for the two trailing RAI fields in V2.
	 * Simplify the implementation by always constructing the V2 response, and
	 * just trimming the response size for V1.
	 */
	struct rpc_lte_state_v2_response rsp = {0};
	size_t rsp_size = v2 ? sizeof(struct rpc_lte_state_v2_response)
			     : sizeof(struct rpc_lte_state_response);

	if (iface == NULL) {
		return rpc_response_simple_req(request, -EINVAL, &rsp, rsp_size);
	}

	/* Common networking state */
	rpc_common_net_query(iface, &rsp.common);

	/* LTE specific state */
	lte_modem_lte_state(&rsp.lte);

	/* Allocate and return the response */
	return rpc_response_simple_req(request, 0, &rsp, rsp_size);
}

struct net_buf *rpc_command_lte_state(struct net_buf *request)
{
	return rpc_command_lte_state_common(request, false);
}

struct net_buf *rpc_command_lte_state_v2(struct net_buf *request)
{
	return rpc_command_lte_state_common(request, true);
}
