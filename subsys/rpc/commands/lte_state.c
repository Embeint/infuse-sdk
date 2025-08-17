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
#include <infuse/lib/nrf_modem_monitor.h>

#include <nrf_modem_at.h>

#include "common_net_query.h"

LOG_MODULE_DECLARE(rpc_server);

static void nrf_modem_lte_state(struct rpc_struct_lte_state *lte)
{
	struct nrf_modem_network_state state;
	int16_t rsrp;
	int8_t rsrq;

	/* Generic network state */
	nrf_modem_monitor_network_state(&state);

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

	/* Current signal state */
	(void)nrf_modem_monitor_signal_quality(&rsrp, &rsrq, false);
	lte->rsrp = rsrp;
	lte->rsrq = rsrq;
}

struct net_buf *rpc_command_lte_state(struct net_buf *request)
{
	struct net_if *iface = net_if_get_first_by_type(&(NET_L2_GET_NAME(OFFLOADED_NETDEV)));
	struct rpc_lte_state_response rsp = {0};

	if (iface == NULL) {
		return rpc_response_simple_req(request, -EINVAL, &rsp, sizeof(rsp));
	}

	/* Common networking state */
	rpc_common_net_query(iface, &rsp.common);

	/* LTE specific state */
	nrf_modem_lte_state(&rsp.lte);

	/* Allocate and return the response */
	return rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));
}
