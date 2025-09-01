/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <time.h>
#include <stdlib.h>

#include <zephyr/ztest.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/conn_mgr_connectivity.h>

#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/reboot.h>
#include <infuse/lib/nrf_modem_monitor.h>
#include <infuse/lib/nrf_modem_lib_sim.h>
#include <infuse/tdf/tdf.h>
#include <infuse/tdf/definitions.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/epacket/interface/epacket_dummy.h>

#include <nrf_modem.h>
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>
#include <modem/pdn.h>

K_SEM_DEFINE(reboot_request, 0, 1);

void infuse_reboot(enum infuse_reboot_reason reason, uint32_t info1, uint32_t info2)
{
	k_sem_give(&reboot_request);
}

void infuse_reboot_delayed(enum infuse_reboot_reason reason, uint32_t info1, uint32_t info2,
			   k_timeout_t delay)
{
	k_sem_give(&reboot_request);
}

static void kv_string_equal(uint16_t key, const char *expected_string)
{
	KV_STRUCT_KV_STRING_VAR(64) string;
	int rc;

	rc = kv_store_read(key, &string, sizeof(string));
	zassert_equal(strlen(expected_string) + 2, rc);
	zassert_equal(strlen(expected_string) + 1, string.value_num);
	zassert_mem_equal(expected_string, string.value, strlen(expected_string));
}

static void test_signal_strength(void)
{
	int16_t rsrp;
	int8_t rsrq;
	int rc;

	/* Initial values */
	rc = nrf_modem_monitor_signal_quality(&rsrp, &rsrq, true);
	zassert_equal(0, rc);
	zassert_equal(INT16_MIN, rsrp);
	zassert_equal(INT8_MIN, rsrq);
	rc = nrf_modem_monitor_signal_quality(&rsrp, &rsrq, false);
	zassert_equal(0, rc);
	zassert_equal(INT16_MIN, rsrp);
	zassert_equal(INT8_MIN, rsrq);

	/* Let values be reported */
	nrf_modem_lib_sim_signal_strength(32, 2);
	rc = nrf_modem_monitor_signal_quality(&rsrp, &rsrq, false);
	zassert_equal(0, rc);
	zassert_equal(-139, rsrp);
	zassert_equal(-4, rsrq);
	rc = nrf_modem_monitor_signal_quality(&rsrp, &rsrq, true);
	zassert_equal(0, rc);
	zassert_equal(-139, rsrp);
	zassert_equal(-4, rsrq);

	/* Revert to unknown, cache should be preserved */
	nrf_modem_lib_sim_signal_strength(255, 255);
	rc = nrf_modem_monitor_signal_quality(&rsrp, &rsrq, false);
	zassert_equal(0, rc);
	zassert_equal(INT16_MIN, rsrp);
	zassert_equal(INT8_MIN, rsrq);
	rc = nrf_modem_monitor_signal_quality(&rsrp, &rsrq, true);
	zassert_equal(0, rc);
	zassert_equal(-139, rsrp);
	zassert_equal(-4, rsrq);
}

static void test_at_safe(void)
{
	int16_t rsrp;
	int8_t rsrq;
	int rc;

	/* Safe by default */
	zassert_true(nrf_modem_monitor_is_at_safe());

	/* BIP connecting */
	nrf_modem_lib_sim_send_at("%USATEV: BIP Connecting\r\n");
	k_sleep(K_MSEC(10));
	zassert_false(nrf_modem_monitor_is_at_safe());

	/* Can't query signal quality while AT blocked */
	nrf_modem_lib_sim_signal_strength(32, 2);
	rc = nrf_modem_monitor_signal_quality(&rsrp, &rsrq, false);
	zassert_equal(0, rc);
	zassert_equal(INT16_MIN, rsrp);
	zassert_equal(INT8_MIN, rsrq);
	nrf_modem_lib_sim_signal_strength(255, 255);

	/* BIP connected */
	nrf_modem_lib_sim_send_at("%USATEV: BIP Connected\r\n");
	k_sleep(K_MSEC(10));
	zassert_true(nrf_modem_monitor_is_at_safe());

	/* BIP connected */
	nrf_modem_lib_sim_send_at("%USATEV: BIP Closed\r\n");
	k_sleep(K_MSEC(10));
	zassert_true(nrf_modem_monitor_is_at_safe());
}

static void test_connectivity_stats(void)
{
	int tx_kb, rx_kb, rc;

	/* Hardcoded values from simulator */
	rc = nrf_modem_monitor_connectivity_stats(&tx_kb, &rx_kb);
	zassert_equal(0, rc);
	zassert_equal(18, tx_kb);
	zassert_equal(6, rx_kb);

	/* Querying fails while BIP connecting */
	nrf_modem_lib_sim_send_at("%USATEV: BIP Connecting\r\n");
	k_sleep(K_MSEC(10));

	rc = nrf_modem_monitor_connectivity_stats(&tx_kb, &rx_kb);
	zassert_equal(-EAGAIN, rc);

	nrf_modem_lib_sim_send_at("%USATEV: BIP Closed\r\n");
	k_sleep(K_MSEC(10));

	rc = nrf_modem_monitor_connectivity_stats(&tx_kb, &rx_kb);
	zassert_equal(0, rc);
	zassert_equal(18, tx_kb);
	zassert_equal(6, rx_kb);
}

ZTEST(infuse_nrf_modem_monitor, test_integration)
{
	KV_KEY_TYPE_VAR(KV_KEY_LTE_PDP_CONFIG, 16) pdp_config;
	KV_KEY_TYPE(KV_KEY_LTE_NETWORKING_MODES) net_modes;
	KV_KEY_TYPE(KV_KEY_LTE_MODEM_IMEI) imei;
	KV_KEY_TYPE(KV_KEY_LTE_SIM_IMSI) imsi;
	struct net_if *iface = net_if_get_default();
	struct nrf_modem_network_state net_state;
	struct nrf_modem_fault_info fault_info = {0};
	enum pdn_fam default_family;
	const char *default_apn;
	int rc;

#ifdef CONFIG_INFUSE_NRF_MODEM_MONITOR_CONN_STATE_LOG
	struct tdf_lte_conn_status *lte_conn_status;
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct tdf_parsed tdf;
	struct net_buf *tx;

	zassert_not_null(tx_fifo);
	tx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_is_null(tx);

	/* Enable conn status logging */
	nrf_modem_monitor_network_state_log(TDF_DATA_LOGGER_SERIAL);
#endif

	zassert_false(kv_store_key_exists(KV_KEY_LTE_MODEM_MODEL));
	zassert_false(kv_store_key_exists(KV_KEY_LTE_MODEM_FIRMWARE_REVISION));
	zassert_false(kv_store_key_exists(KV_KEY_LTE_MODEM_ESN));
	zassert_false(kv_store_key_exists(KV_KEY_LTE_MODEM_IMEI));
	zassert_false(kv_store_key_exists(KV_KEY_LTE_SIM_UICC));
	zassert_false(kv_store_key_exists(KV_KEY_LTE_SIM_IMSI));
	zassert_false(kv_store_key_exists(KV_KEY_LTE_PDP_CONFIG));
	zassert_false(kv_store_key_exists(KV_KEY_LTE_NETWORKING_MODES));

	zassert_false(nrf_modem_is_initialized());
	rc = nrf_modem_lib_init();
	zassert_equal(0, rc);
	zassert_true(nrf_modem_is_initialized());

	zassert_true(kv_store_key_exists(KV_KEY_LTE_MODEM_MODEL));
	zassert_true(kv_store_key_exists(KV_KEY_LTE_MODEM_FIRMWARE_REVISION));
	zassert_true(kv_store_key_exists(KV_KEY_LTE_MODEM_ESN));
	zassert_true(kv_store_key_exists(KV_KEY_LTE_MODEM_IMEI));
	zassert_false(kv_store_key_exists(KV_KEY_LTE_SIM_UICC));
	zassert_false(kv_store_key_exists(KV_KEY_LTE_SIM_IMSI));
	zassert_true(kv_store_key_exists(KV_KEY_LTE_PDP_CONFIG));
	zassert_true(kv_store_key_exists(KV_KEY_LTE_NETWORKING_MODES));

	kv_string_equal(KV_KEY_LTE_MODEM_MODEL, CONFIG_INFUSE_NRF_MODEM_LIB_SIM_MODEL);
	kv_string_equal(KV_KEY_LTE_MODEM_FIRMWARE_REVISION,
			CONFIG_INFUSE_NRF_MODEM_LIB_SIM_FIRMWARE);
	kv_string_equal(KV_KEY_LTE_MODEM_ESN, CONFIG_INFUSE_NRF_MODEM_LIB_SIM_ESN);
	KV_STORE_READ(KV_KEY_LTE_MODEM_IMEI, &imei);
	zassert_equal(atoll(CONFIG_INFUSE_NRF_MODEM_LIB_SIM_IMEI), imei.imei);
	KV_STORE_READ(KV_KEY_LTE_NETWORKING_MODES, &net_modes);
	zassert_equal(LTE_LC_SYSTEM_MODE_LTEM_NBIOT_GPS, net_modes.modes);
	zassert_equal(CONFIG_LTE_MODE_PREFERENCE_VALUE, net_modes.prefer);
	kv_store_read(KV_KEY_LTE_PDP_CONFIG, &pdp_config, sizeof(pdp_config));
	zassert_equal(PDN_FAM_IPV4V6, pdp_config.family);
	zassert_mem_equal(CONFIG_INFUSE_NRF_MODEM_MONITOR_DEFAULT_PDP_APN, pdp_config.apn.value,
			  strlen(CONFIG_INFUSE_NRF_MODEM_MONITOR_DEFAULT_PDP_APN));

	nrf_modem_lib_sim_default_pdn_ctx(&default_apn, &default_family);
	zassert_mem_equal(CONFIG_INFUSE_NRF_MODEM_MONITOR_DEFAULT_PDP_APN, default_apn,
			  strlen(CONFIG_INFUSE_NRF_MODEM_MONITOR_DEFAULT_PDP_APN));
	zassert_equal(PDN_FAM_IPV4V6, default_family);

	nrf_modem_monitor_network_state(&net_state);
	zassert_equal(LTE_LC_NW_REG_NOT_REGISTERED, net_state.nw_reg_status);

	/* Searching for a second */
	nrf_modem_lib_sim_send_at("+CEREG: 2,\"702A\",\"08C3BD0C\",7\r\n");
	k_sleep(K_SECONDS(1));

	/* SIM card queried now that LTE is active */
	zassert_true(kv_store_key_exists(KV_KEY_LTE_SIM_UICC));
	kv_string_equal(KV_KEY_LTE_SIM_UICC, CONFIG_INFUSE_NRF_MODEM_LIB_SIM_UICC);
	zassert_true(kv_store_key_exists(KV_KEY_LTE_SIM_IMSI));
	KV_STORE_READ(KV_KEY_LTE_SIM_IMSI, &imsi);
	zassert_equal(atoll(CONFIG_INFUSE_NRF_MODEM_LIB_SIM_IMSI), imsi.imsi);

	nrf_modem_monitor_network_state(&net_state);
	zassert_equal(LTE_LC_NW_REG_SEARCHING, net_state.nw_reg_status);
	zassert_equal(0x702A, net_state.cell.tac);
	zassert_equal(0x08C3BD0C, net_state.cell.id);

#ifdef CONFIG_INFUSE_NRF_MODEM_MONITOR_CONN_STATE_LOG
	k_sleep(K_MSEC(10));
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	tx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_not_null(tx);
	net_buf_pull(tx, sizeof(struct epacket_dummy_frame));
	zassert_equal(0, tdf_parse_find_in_buf(tx->data, tx->len, TDF_LTE_CONN_STATUS, &tdf));
	lte_conn_status = tdf.data;
	zassert_equal(0x702A, lte_conn_status->cell.tac);
	zassert_equal(0x08C3BD0C, lte_conn_status->cell.eci);
	zassert_equal(2, lte_conn_status->status);
	net_buf_unref(tx);
#endif

	test_signal_strength();
	test_at_safe();
	test_connectivity_stats();

	/* Connected to network */
	nrf_modem_lib_sim_send_at("+CSCON: 1\r\n");
	k_sleep(K_SECONDS(1));

	/* Cell search complete */
	nrf_modem_lib_sim_send_at("%MDMEV: SEARCH STATUS 2\r\n");

	/* Registered to network (XMONITOR response hardcoded in simulator) */
	nrf_modem_lib_sim_send_at(
		"+CEREG: 5,\"702A\",\"08C3BD0C\",7,,,\"00001000\",\"00101101\"\r\n");
	k_sleep(K_SECONDS(1));

	nrf_modem_monitor_network_state(&net_state);
	zassert_equal(LTE_LC_NW_REG_REGISTERED_ROAMING, net_state.nw_reg_status);
	zassert_equal(0x702A, net_state.cell.tac);
	zassert_equal(0x08C3BD0C, net_state.cell.id);
	zassert_equal(103, net_state.cell.phys_cell_id);
	zassert_equal(505, net_state.cell.mcc);
	zassert_equal(1, net_state.cell.mnc);
	zassert_equal(9410, net_state.cell.earfcn);
	zassert_equal(LTE_LC_LTE_MODE_LTEM, net_state.lte_mode);
	zassert_equal(28, net_state.band);
	zassert_equal(16, net_state.psm_cfg.active_time);
	zassert_equal(46800, net_state.psm_cfg.tau);
	zassert_equal(0, net_state.edrx_cfg.mode);
	zassert_equal(-1.0f, net_state.edrx_cfg.edrx);
	zassert_equal(-1.0f, net_state.edrx_cfg.ptw);

#ifdef CONFIG_INFUSE_NRF_MODEM_MONITOR_CONN_STATE_LOG
	k_sleep(K_MSEC(10));
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	tx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_not_null(tx);
	net_buf_pull(tx, sizeof(struct epacket_dummy_frame));
	zassert_equal(0, tdf_parse_find_in_buf(tx->data, tx->len, TDF_LTE_CONN_STATUS, &tdf));
	lte_conn_status = tdf.data;
	zassert_equal(0x702A, lte_conn_status->cell.tac);
	zassert_equal(0x08C3BD0C, lte_conn_status->cell.eci);
	zassert_equal(5, lte_conn_status->status);

	net_buf_unref(tx);
#endif

	/* eDRX configuration */
	nrf_modem_lib_sim_send_at("+CEDRXP: 4,\"0001\",\"0001\",\"0001\"\r\n");
	k_sleep(K_SECONDS(1));
	nrf_modem_monitor_network_state(&net_state);
	zassert_equal(LTE_LC_LTE_MODE_LTEM, net_state.edrx_cfg.mode);
	zassert_within(10.24f, net_state.edrx_cfg.edrx, 0.01f);
	zassert_within(2.56f, net_state.edrx_cfg.ptw, 0.01f);

	/* If no connectivity is gained in required timeout, expect a reboot to be requested */
	rc = k_sem_take(&reboot_request,
			K_SECONDS(CONFIG_INFUSE_NRF_MODEM_MONITOR_CONNECTIVITY_TIMEOUT_SEC + 1));
	zassert_equal(0, rc);

	/* Revert to searching */
	nrf_modem_lib_sim_send_at("+CEREG: 2,\"702A\",\"08C3BD0C\",7\r\n");
	k_sleep(K_SECONDS(1));

#ifdef CONFIG_INFUSE_NRF_MODEM_MONITOR_CONN_STATE_LOG
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	tx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_not_null(tx);
	net_buf_pull(tx, sizeof(struct epacket_dummy_frame));
	zassert_equal(0, tdf_parse_find_in_buf(tx->data, tx->len, TDF_LTE_CONN_STATUS, &tdf));
	lte_conn_status = tdf.data;
	zassert_equal(0x702A, lte_conn_status->cell.tac);
	zassert_equal(0x08C3BD0C, lte_conn_status->cell.eci);
	zassert_equal(2, lte_conn_status->status);
	net_buf_unref(tx);

	/* Disable conn status logging */
	nrf_modem_monitor_network_state_log(0);
#endif

	/* Back on the network, gain network connectivity this time */
	nrf_modem_lib_sim_send_at(
		"+CEREG: 5,\"702A\",\"08C3BD0C\",7,,,\"00001000\",\"00101101\"\r\n");
	k_sleep(K_SECONDS(1));
	rc = net_if_up(iface);
	zassert_equal(0, rc);

	/* No connectivity timeout */
	rc = k_sem_take(&reboot_request,
			K_SECONDS(CONFIG_INFUSE_NRF_MODEM_MONITOR_CONNECTIVITY_TIMEOUT_SEC + 1));
	zassert_equal(-EAGAIN, rc);

	/* Cell changes while BIP is pending */
	nrf_modem_lib_sim_send_at("%USATEV: BIP Connecting\r\n");
	k_sleep(K_MSEC(10));
	nrf_modem_lib_sim_send_at(
		"+CEREG: 5,\"702B\",\"08C3BD0D\",9,,,\"00001000\",\"00101101\"\r\n");
	k_sleep(K_SECONDS(1));
	nrf_modem_lib_sim_send_at("%USATEV: BIP Closed\r\n");
	k_sleep(K_SECONDS(2));

	nrf_modem_monitor_network_state(&net_state);
	zassert_equal(LTE_LC_LTE_MODE_NBIOT, net_state.lte_mode);
	zassert_equal(0x702B, net_state.cell.tac);
	zassert_equal(0x08C3BD0D, net_state.cell.id);

	/* RRC idle, then modem sleep */
	nrf_modem_lib_sim_send_at("+CSCON: 0\r\n");
	k_sleep(K_SECONDS(5));
	nrf_modem_lib_sim_send_at("%XMODEMSLEEP: 1,46783975\r\n");
	k_sleep(K_SECONDS(10));

	/* Modem wakes */
	nrf_modem_lib_sim_send_at("%XMODEMSLEEP: 1,0\r\n");
	/* Network connectivity goes down, reboot should be requested */
	rc = net_if_down(iface);
	zassert_equal(0, rc);
	rc = k_sem_take(&reboot_request,
			K_SECONDS(CONFIG_INFUSE_NRF_MODEM_MONITOR_CONNECTIVITY_TIMEOUT_SEC + 1));
	zassert_equal(0, rc);

	/* Back to searching */
	nrf_modem_lib_sim_send_at("+CEREG: 2,\"702A\",\"08C3BD0C\",7\r\n");
	k_sleep(K_SECONDS(1));

	/* Registration gained then lost, no timeout */
	nrf_modem_lib_sim_send_at(
		"+CEREG: 5,\"702A\",\"08C3BD0C\",7,,,\"00001000\",\"00101101\"\r\n");
	k_sleep(K_SECONDS(1));
	nrf_modem_lib_sim_send_at("+CEREG: 2,\"702A\",\"08C3BD0C\",7\r\n");

	rc = k_sem_take(&reboot_request,
			K_SECONDS(CONFIG_INFUSE_NRF_MODEM_MONITOR_CONNECTIVITY_TIMEOUT_SEC + 1));
	zassert_equal(-EAGAIN, rc);

	/* Connectivity gained, lost with inverse ordering, no timeout */
	nrf_modem_lib_sim_send_at(
		"+CEREG: 5,\"702A\",\"08C3BD0C\",7,,,\"00001000\",\"00101101\"\r\n");
	k_sleep(K_SECONDS(1));
	rc = net_if_up(iface);
	zassert_equal(0, rc);
	k_sleep(K_SECONDS(1));
	nrf_modem_lib_sim_send_at("+CEREG: 2,\"702A\",\"08C3BD0C\",7\r\n");
	rc = net_if_down(iface);
	zassert_equal(0, rc);
	k_sleep(K_SECONDS(1));
	rc = k_sem_take(&reboot_request,
			K_SECONDS(CONFIG_INFUSE_NRF_MODEM_MONITOR_CONNECTIVITY_TIMEOUT_SEC + 1));
	zassert_equal(-EAGAIN, rc);

#ifdef CONFIG_INFUSE_NRF_MODEM_MONITOR_CONN_STATE_LOG
	/* No other logging after disabling */
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	tx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_is_null(tx);
#endif /* CONFIG_INFUSE_NRF_MODEM_MONITOR_CONN_STATE_LOG */

	/* Changing APN configuration should request a reboot */
	memcpy(pdp_config.apn.value, "upd", 3);
	zassert_equal(-EBUSY, k_sem_take(&reboot_request, K_NO_WAIT));
	kv_store_write(KV_KEY_LTE_PDP_CONFIG, &pdp_config, sizeof(pdp_config));
	zassert_equal(0, k_sem_take(&reboot_request, K_SECONDS(1)));

	/* Changing network configuration should request a reboot */
	net_modes.modes = LTE_LC_SYSTEM_MODE_LTEM;
	zassert_equal(-EBUSY, k_sem_take(&reboot_request, K_NO_WAIT));
	kv_store_write(KV_KEY_LTE_NETWORKING_MODES, &net_modes, sizeof(net_modes));
	zassert_equal(0, k_sem_take(&reboot_request, K_SECONDS(1)));

	/* Modem fault should request a reboot */
	nrf_modem_fault_handler(&fault_info);
	zassert_equal(0, k_sem_take(&reboot_request, K_SECONDS(1)));
}

void *test_init(void)
{
	struct net_if *iface = net_if_get_default();

	kv_store_reset();
	net_if_down(iface);

	return NULL;
}

ZTEST_SUITE(infuse_nrf_modem_monitor, NULL, test_init, NULL, NULL, NULL);
