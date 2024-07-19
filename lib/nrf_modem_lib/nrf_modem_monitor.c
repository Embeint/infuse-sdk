/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <stdlib.h>

#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <infuse/lib/nrf_modem_monitor.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/reboot.h>

#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>
#include <modem/modem_info.h>
#include <nrf_modem_at.h>

LOG_MODULE_REGISTER(modem_monitor, LOG_LEVEL_INF);

static struct nrf_modem_network_state latest_state;

void nrf_modem_monitor_network_state(struct nrf_modem_network_state *state)
{
	*state = latest_state;
}

static void network_info_update(void)
{
	static bool sim_card_queried;
	char plmn[9] = {0};
	int rc;

	if (!sim_card_queried) {
		KV_STRUCT_KV_STRING_VAR(24) sim_uicc;

		rc = nrf_modem_at_scanf("AT%XICCID", "%%XICCID: %24s", &sim_uicc.value);
		if (rc == 1) {
			sim_uicc.value_num = strlen(sim_uicc.value) + 1;
			if (kv_store_write(KV_KEY_LTE_SIM_UICC, &sim_uicc, 1 + sim_uicc.value_num) >
			    0) {
				/* Print value when first saved to KV store */
				LOG_INF("SIM: %s", sim_uicc.value);
			}
			sim_card_queried = true;
		}
	}

	if ((latest_state.nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
	    (latest_state.nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING)) {
		/* No cell information */
		memset(&latest_state.cell, 0x00, sizeof(latest_state.cell));
		latest_state.edrx_cfg.edrx = -1.0f;
		latest_state.edrx_cfg.ptw = -1.0f;
		return;
	}

	/* Query state from the modem */
	rc = nrf_modem_at_scanf("AT%XMONITOR",
				"%%XMONITOR: "
				"%*u,"         /* <reg_status>: ignored */
				"%*[^,],"      /* <full_name>: ignored */
				"%*[^,],"      /* <short_name>: ignored */
				"%9[^,],"      /* <plmn> */
				"%*[^,],"      /* <tac>: ignored */
				"%*d,"         /* <AcT>: ignored */
				"%" SCNu8 ","  /* <band> */
				"%*[^,],"      /* <cell_id>: ignored */
				"%" SCNu16 "," /* <phys_cell_id> */
				"%" SCNu16 "," /* <EARFCN> */
				,
				plmn, &latest_state.band, &latest_state.cell.phys_cell_id,
				&latest_state.cell.earfcn);
	if (rc == 4) {
		uint8_t sep = 0;
		/* Parse MCC and MNC */
		if (plmn[7] == '\x00') {
			/* 3 character MCC, 2 character MNC */
			plmn[6] = '\x00';
			sep = 4;
		} else if (plmn[8] == '\x00') {
			/* 3 character MCC, 3 character MNC */
			plmn[7] = '\x00';
			sep = 3;
		}
		latest_state.cell.mnc = atoi(&plmn[sep]);
		plmn[sep] = '\x00';
		latest_state.cell.mcc = atoi(&plmn[1]);
	}
}

int nrf_modem_monitor_signal_quality(int16_t *rsrp, int8_t *rsrq)
{
	uint8_t rsrp_idx, rsrq_idx;
	int rc;

	*rsrp = 0;
	*rsrq = 0;

	/* Query state from the modem */
	rc = nrf_modem_at_scanf("AT+CESQ", "+CESQ: %*d,%*d,%*d,%*d,%" SCNu8 ",%" SCNu8, &rsrp_idx,
				&rsrq_idx);
	if (rc == 2) {
		/* Convert from index to physical units if known */
		if (rsrp_idx != 255) {
			*rsrp = RSRP_IDX_TO_DBM(rsrp_idx);
		}
		if (rsrq_idx != 255) {
			*rsrq = RSRQ_IDX_TO_DB(rsrq_idx);
		}
		return 0;
	}

	return -EAGAIN;
}

static void lte_reg_handler(const struct lte_lc_evt *const evt)
{
	switch (evt->type) {
	case LTE_LC_EVT_NW_REG_STATUS:
		LOG_DBG("NW_REG_STATUS");
		LOG_DBG("  STATUS: %d", evt->nw_reg_status);
		latest_state.nw_reg_status = evt->nw_reg_status;
		/* Update knowledge of network info */
		network_info_update();
		break;
	case LTE_LC_EVT_PSM_UPDATE:
		LOG_DBG("PSM_UPDATE");
		LOG_DBG("     TAU: %d", evt->psm_cfg.tau);
		LOG_DBG("  ACTIVE: %d", evt->psm_cfg.active_time);
		latest_state.psm_cfg = evt->psm_cfg;
		break;
	case LTE_LC_EVT_EDRX_UPDATE:
		LOG_DBG("EDRX_UPDATE");
		LOG_DBG("    Mode: %d", evt->edrx_cfg.mode);
		LOG_DBG("     PTW: %d", (int)evt->edrx_cfg.ptw);
		LOG_DBG("Interval: %d", (int)evt->edrx_cfg.edrx);
		latest_state.edrx_cfg = evt->edrx_cfg;
		break;
	case LTE_LC_EVT_RRC_UPDATE:
		LOG_DBG("RRC_UPDATE");
		LOG_DBG("   State: %s", evt->rrc_mode == LTE_LC_RRC_MODE_IDLE ? "Idle" : "Active");
		latest_state.rrc_mode = evt->rrc_mode;
		break;
	case LTE_LC_EVT_CELL_UPDATE:
		LOG_DBG("CELL_UPDATE");
		LOG_DBG("     TAC: %d", evt->cell.tac);
		LOG_DBG("      ID: %d", evt->cell.id);
		/* Update knowledge of network info */
		network_info_update();
		/* Set cell info */
		latest_state.cell.tac = evt->cell.tac;
		latest_state.cell.id = evt->cell.id;
		break;
	case LTE_LC_EVT_LTE_MODE_UPDATE:
		LOG_DBG("LTE_MODE_UPDATE");
		LOG_DBG("    Mode: %d", evt->lte_mode);
		latest_state.lte_mode = evt->lte_mode;
		break;
	case LTE_LC_EVT_MODEM_SLEEP_ENTER:
		LOG_DBG("MODEM_SLEEP_ENTER");
		LOG_DBG("    Type: %d", evt->modem_sleep.type);
		LOG_DBG("     Dur: %lld", evt->modem_sleep.time);
		break;
	case LTE_LC_EVT_MODEM_SLEEP_EXIT:
		LOG_DBG("MODEM_SLEEP_EXIT");
		LOG_DBG("    Type: %d", evt->modem_sleep.type);
		break;
	default:
		LOG_DBG("LTE EVENT: %d", evt->type);
		break;
	}
}

LTE_LC_ON_CFUN(infuse_cfun_hook, infuse_modem_info, NULL);

static void infuse_modem_info(enum lte_lc_func_mode mode, void *ctx)
{
	KV_STRUCT_KV_STRING_VAR(64) modem_info = {0};
	KV_KEY_TYPE(KV_KEY_LTE_MODEM_IMEI) modem_imei;
	static bool modem_info_stored;

	if (modem_info_stored) {
		return;
	}
	/* Model identifier */
	nrf_modem_at_scanf("AT+CGMM", "%64s\n", &modem_info.value);
	modem_info.value_num = strlen(modem_info.value) + 1;
	(void)kv_store_write(KV_KEY_LTE_MODEM_MODEL, &modem_info, 1 + modem_info.value_num);
	/* Modem firmware revision */
	nrf_modem_at_scanf("AT+CGMR", "%64s\n", &modem_info.value);
	modem_info.value_num = strlen(modem_info.value) + 1;
	(void)kv_store_write(KV_KEY_LTE_MODEM_FIRMWARE_REVISION, &modem_info,
			     1 + modem_info.value_num);
	/* Modem ESN */
	nrf_modem_at_scanf("AT+CGSN=0", "%64s\n", &modem_info.value);
	modem_info.value_num = strlen(modem_info.value) + 1;
	(void)kv_store_write(KV_KEY_LTE_MODEM_ESN, &modem_info, 1 + modem_info.value_num);
	/* Modem IMEI */
	nrf_modem_at_scanf("AT+CGSN=1", "+CGSN: \"%" SCNd64 "\"\n", &modem_imei.imei);
	(void)KV_STORE_WRITE(KV_KEY_LTE_MODEM_IMEI, &modem_imei);
	/* Modem info has been stored */
	modem_info_stored = true;
}

void lte_net_if_modem_fault_app_handler(struct nrf_modem_fault_info *fault_info)
{
#ifdef CONFIG_INFUSE_REBOOT
	/* Handling any fault properly is uncertain, safest option is to trigger a reboot */
	LOG_ERR("Modem fault, rebooting in 2 seconds...");
	infuse_reboot_delayed(INFUSE_REBOOT_LTE_MODEM_FAULT, fault_info->program_counter,
			      fault_info->reason, K_SECONDS(2));
#else
	LOG_ERR("Modem fault, no reboot support!");
#endif /* CONFIG_INFUSE_REBOOT */
}

int nrf_modem_monitor_init(void)
{
	/* Initial state */
	latest_state.edrx_cfg.edrx = -1.0f;
	latest_state.edrx_cfg.ptw = -1.0f;
	/* Register handler */
	lte_lc_register_handler(lte_reg_handler);
	return 0;
}

SYS_INIT(nrf_modem_monitor_init, APPLICATION, 0);
