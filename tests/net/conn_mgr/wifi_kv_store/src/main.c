/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <time.h>

#include <zephyr/ztest.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/conn_mgr_connectivity.h>

#include <infuse/drivers/wifi/wifi_sim.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/net/dns.h>

static void validate_initial(struct net_if *iface)
{
	zassert_not_null(iface);
	zassert_false(net_if_is_admin_up(iface));
	zassert_false(net_if_is_carrier_ok(iface));
	zassert_true(net_if_is_dormant(iface));
}

static void validate_connected(struct net_if *iface)
{
	zassert_true(net_if_is_admin_up(iface));
	zassert_true(net_if_is_carrier_ok(iface));
	zassert_false(net_if_is_dormant(iface));
}

static void validate_disconnected(struct net_if *iface, bool admin_up, bool carrier_up)
{
	zassert_equal(admin_up, net_if_is_admin_up(iface));
	zassert_equal(carrier_up, net_if_is_carrier_ok(iface));
	zassert_true(net_if_is_dormant(iface));
}

ZTEST(wifi_kv_store, test_no_configuration)
{
	struct net_if *iface = net_if_get_first_wifi();

	validate_initial(iface);

	/* Turn on all interfaces */
	conn_mgr_all_if_up(true);
	conn_mgr_all_if_connect(true);

	/* No WiFi configuration, nothing should happen */
	k_sleep(K_SECONDS(2));
	validate_disconnected(iface, true, true);

	conn_mgr_all_if_disconnect(true);
	conn_mgr_all_if_down(true);
}

ZTEST(wifi_kv_store, test_configured_on_boot)
{
	KV_STRING_CONST(ssid, CONFIG_WIFI_SIM_AP_SSID);
	KV_STRING_CONST(psk, CONFIG_WIFI_SIM_AP_PSK);
	struct net_if *iface = net_if_get_first_wifi();

	validate_initial(iface);

	zassert_equal(sizeof(ssid), kv_store_write(KV_KEY_WIFI_SSID, &ssid, sizeof(ssid)));
	zassert_equal(sizeof(psk), kv_store_write(KV_KEY_WIFI_PSK, &psk, sizeof(psk)));
	k_sleep(K_MSEC(200));

	/* Turn on all interfaces */
	conn_mgr_all_if_up(true);
	conn_mgr_all_if_connect(true);

	/* Should now be connected */
	k_sleep(K_SECONDS(1));
	validate_connected(iface);

	/* Turn off all interfaces */
	conn_mgr_all_if_disconnect(true);
	/* Delay required due to the instantaneous behaviour of posix */
	k_sleep(K_MSEC(10));
	conn_mgr_all_if_down(true);
	k_sleep(K_MSEC(10));
}

ZTEST(wifi_kv_store, test_configured_on_boot_retried)
{
	KV_STRING_CONST(ssid, CONFIG_WIFI_SIM_AP_SSID);
	KV_STRING_CONST(psk, CONFIG_WIFI_SIM_AP_PSK);
	struct net_if *iface = net_if_get_first_wifi();

	validate_initial(iface);

	zassert_equal(sizeof(ssid), kv_store_write(KV_KEY_WIFI_SSID, &ssid, sizeof(ssid)));
	zassert_equal(sizeof(psk), kv_store_write(KV_KEY_WIFI_PSK, &psk, sizeof(psk)));
	k_sleep(K_MSEC(200));

	/* Not in range to start with */
	wifi_sim_in_network_range(false);

	/* Turn on all interfaces */
	conn_mgr_all_if_up(true);
	conn_mgr_all_if_connect(true);

	/* There is no connection timeout, so individual timeouts should be retried */
	k_sleep(K_SECONDS(6));
	wifi_sim_in_network_range(true);

	/* Should now be connected */
	k_sleep(K_SECONDS(2));
	validate_connected(iface);

	/* Simulate a disconnect */
	wifi_sim_trigger_disconnect();

	/* Not persistent, should not attempt to reconnect, admin and carrier down */
	k_sleep(K_SECONDS(2));
	validate_disconnected(iface, false, false);

	/* Turn off all interfaces */
	conn_mgr_all_if_disconnect(true);
	/* Delay required due to the instantaneous behaviour of posix */
	k_sleep(K_MSEC(10));
	conn_mgr_all_if_down(true);
	k_sleep(K_MSEC(10));
}

ZTEST(wifi_kv_store, test_configured_on_boot_timeout)
{
	KV_STRING_CONST(ssid, CONFIG_WIFI_SIM_AP_SSID);
	KV_STRING_CONST(psk, CONFIG_WIFI_SIM_AP_PSK);
	struct net_if *iface = net_if_get_first_wifi();

	validate_initial(iface);

	zassert_equal(sizeof(ssid), kv_store_write(KV_KEY_WIFI_SSID, &ssid, sizeof(ssid)));
	zassert_equal(sizeof(psk), kv_store_write(KV_KEY_WIFI_PSK, &psk, sizeof(psk)));
	k_sleep(K_MSEC(200));

	/* Not in range to start with */
	wifi_sim_in_network_range(false);

	/* Connection timeout is 4 seconds */
	zassert_equal(0, conn_mgr_if_set_timeout(iface, 4));

	/* Turn on all interfaces */
	conn_mgr_all_if_up(true);
	conn_mgr_all_if_connect(true);

	k_sleep(K_SECONDS(1));
	zassert_true(net_if_is_admin_up(iface));

	/* Connection should have timed out and taken interfaces down */
	k_sleep(K_SECONDS(5));
	zassert_false(net_if_is_admin_up(iface));
	zassert_false(net_if_is_carrier_ok(iface));

	/* Coming back in range shouldn't do anything */
	wifi_sim_in_network_range(true);
	k_sleep(K_SECONDS(3));
	zassert_false(net_if_is_admin_up(iface));
	zassert_false(net_if_is_carrier_ok(iface));
}

ZTEST(wifi_kv_store, test_configured_after_up)
{
	KV_STRING_CONST(ssid, CONFIG_WIFI_SIM_AP_SSID);
	KV_STRING_CONST(psk, CONFIG_WIFI_SIM_AP_PSK);
	struct net_if *iface = net_if_get_first_wifi();

	validate_initial(iface);

	/* Turn on all interfaces */
	conn_mgr_all_if_up(true);
	conn_mgr_all_if_connect(true);

	/* Not connected to start with */
	k_sleep(K_SECONDS(1));
	validate_disconnected(iface, true, true);

	/* Write the network configuration */
	zassert_equal(sizeof(ssid), kv_store_write(KV_KEY_WIFI_SSID, &ssid, sizeof(ssid)));
	zassert_equal(sizeof(psk), kv_store_write(KV_KEY_WIFI_PSK, &psk, sizeof(psk)));

	/* Still not connected, interface is not persistent so not retried */
	k_sleep(K_SECONDS(1));
	validate_disconnected(iface, true, true);

	/* Turn off all interfaces */
	conn_mgr_all_if_disconnect(true);
	/* Delay required due to the instantaneous behaviour of posix */
	k_sleep(K_MSEC(10));
	conn_mgr_all_if_down(true);
	k_sleep(K_MSEC(10));
}

ZTEST(wifi_kv_store, test_persistent_configured_after_up)
{
	KV_STRING_CONST(ssid, CONFIG_WIFI_SIM_AP_SSID);
	KV_STRING_CONST(psk, CONFIG_WIFI_SIM_AP_PSK);
	struct net_if *iface = net_if_get_first_wifi();

	conn_mgr_if_set_flag(iface, CONN_MGR_IF_PERSISTENT, true);

	validate_initial(iface);

	/* Not in range to start with */
	wifi_sim_in_network_range(false);

	/* Turn on all interfaces */
	conn_mgr_all_if_up(true);
	conn_mgr_all_if_connect(true);

	/* Not connected to start with */
	k_sleep(K_SECONDS(1));
	validate_disconnected(iface, true, true);

	/* Write the network configuration */
	zassert_equal(sizeof(ssid), kv_store_write(KV_KEY_WIFI_SSID, &ssid, sizeof(ssid)));
	zassert_equal(sizeof(psk), kv_store_write(KV_KEY_WIFI_PSK, &psk, sizeof(psk)));

	/* Still not connected (AP not in range) */
	k_sleep(K_SECONDS(2));
	validate_disconnected(iface, true, true);

	/* Now back in range */
	wifi_sim_in_network_range(true);

	/* Should now be connected */
	k_sleep(K_SECONDS(2));
	validate_connected(iface);

	/* Simulate a disconnect */
	wifi_sim_trigger_disconnect();

	/* Should automatically reconnect */
	k_sleep(K_SECONDS(2));
	validate_connected(iface);

	/* Delete the configuration */
	kv_store_delete(KV_KEY_WIFI_SSID);
	kv_store_delete(KV_KEY_WIFI_PSK);

	/* Transitions back to disconnected */
	k_sleep(K_SECONDS(1));
	validate_disconnected(iface, true, true);

	/* Turn off all interfaces */
	conn_mgr_all_if_disconnect(true);
	/* Delay required due to the instantaneous behaviour of posix */
	k_sleep(K_MSEC(10));
	conn_mgr_all_if_down(true);
	k_sleep(K_MSEC(10));
}

ZTEST(wifi_kv_store, test_bad_ssid)
{
	KV_STRING_CONST(ssid, CONFIG_WIFI_SIM_AP_SSID "wrong");
	KV_STRING_CONST(psk, CONFIG_WIFI_SIM_AP_PSK);
	struct net_if *iface = net_if_get_first_wifi();

	/* Write the network configuration */
	zassert_equal(sizeof(ssid), kv_store_write(KV_KEY_WIFI_SSID, &ssid, sizeof(ssid)));
	zassert_equal(sizeof(psk), kv_store_write(KV_KEY_WIFI_PSK, &psk, sizeof(psk)));
	k_sleep(K_MSEC(200));

	validate_initial(iface);

	/* Turn on all interfaces */
	conn_mgr_all_if_up(true);
	conn_mgr_all_if_connect(true);

	/* Not connected to start with */
	k_sleep(K_SECONDS(1));
	validate_disconnected(iface, true, true);

	/* Turn off all interfaces */
	conn_mgr_all_if_disconnect(true);
	/* Delay required due to the instantaneous behaviour of posix */
	k_sleep(K_MSEC(10));
	conn_mgr_all_if_down(true);
	k_sleep(K_MSEC(10));
}

ZTEST(wifi_kv_store, test_bad_psk)
{
	KV_STRING_CONST(ssid, CONFIG_WIFI_SIM_AP_SSID);
	KV_STRING_CONST(psk, CONFIG_WIFI_SIM_AP_PSK "wrong");
	struct net_if *iface = net_if_get_first_wifi();

	/* Write the network configuration */
	zassert_equal(sizeof(ssid), kv_store_write(KV_KEY_WIFI_SSID, &ssid, sizeof(ssid)));
	zassert_equal(sizeof(psk), kv_store_write(KV_KEY_WIFI_PSK, &psk, sizeof(psk)));
	k_sleep(K_MSEC(200));

	validate_initial(iface);

	/* Turn on all interfaces */
	conn_mgr_all_if_up(true);
	conn_mgr_all_if_connect(true);

	/* Not connected to start with */
	k_sleep(K_SECONDS(1));
	validate_disconnected(iface, true, true);

	/* Turn off all interfaces */
	conn_mgr_all_if_disconnect(true);
	/* Delay required due to the instantaneous behaviour of posix */
	k_sleep(K_MSEC(10));
	conn_mgr_all_if_down(true);
	k_sleep(K_MSEC(10));
}

static void test_before(void *fixture)
{
	struct net_if *iface = net_if_get_first_wifi();

	wifi_sim_in_network_range(true);
	conn_mgr_if_set_flag(iface, CONN_MGR_IF_NO_AUTO_CONNECT, true);
	conn_mgr_if_set_flag(iface, CONN_MGR_IF_PERSISTENT, false);
	conn_mgr_if_set_timeout(iface, CONN_MGR_IF_NO_TIMEOUT);
	kv_store_delete(KV_KEY_WIFI_SSID);
	kv_store_delete(KV_KEY_WIFI_PSK);
}

ZTEST_SUITE(wifi_kv_store, NULL, NULL, test_before, NULL, NULL);
