/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/conn_mgr_connectivity.h>

#include <infuse/states.h>
#include <infuse/lib/lte_modem_monitor.h>

enum lte_request {
	LTE_REQUEST_DISCONNECT,
	LTE_REQUEST_CONNECT,
};

static void lte_control_work_handler(struct k_work *work);

static K_WORK_DEFINE(lte_control_work, lte_control_work_handler);
static struct infuse_state_cb lte_control_state_cb;
static atomic_t pending_request;
static atomic_t last_request;
static struct net_if *iface;

LOG_MODULE_REGISTER(lte_control, CONFIG_INFUSE_AUTO_LTE_CONTROL_LOG_LEVEL);

static void lte_control_work_handler(struct k_work *work)
{
	enum lte_request request;
	int rc;

	request = atomic_get(&pending_request);

	if (request == LTE_REQUEST_CONNECT) {
		rc = conn_mgr_if_connect(iface);
	} else {
		rc = conn_mgr_if_disconnect(iface);
	}
	if (rc == 0) {
		atomic_set(&last_request, request);
	}
}

static void lte_control_request(enum lte_request request)
{
	atomic_set(&pending_request, request);
	k_work_submit(&lte_control_work);
}

static void lte_control_state_set(enum infuse_state state, bool already, uint16_t timeout,
				  void *user_ctx)
{
	ARG_UNUSED(timeout);
	ARG_UNUSED(user_ctx);

	if ((state == INFUSE_STATE_APPLICATION_ACTIVE) && !already) {
		LOG_INF("Device activated, enable LTE");
		lte_control_request(LTE_REQUEST_CONNECT);
	}
}

static void lte_control_state_cleared(enum infuse_state state, void *user_ctx)
{
	ARG_UNUSED(user_ctx);

	if (state == INFUSE_STATE_APPLICATION_ACTIVE) {
		LOG_INF("Device de-activated, disable LTE");
		lte_control_request(LTE_REQUEST_DISCONNECT);
	}
}

void auto_lte_control_init(void)
{
#if defined(CONFIG_NRF_MODEM_LIB)
	iface = net_if_get_first_by_type(&(NET_L2_GET_NAME(OFFLOADED_NETDEV)));
#elif defined(CONFIG_NET_L2_PPP)
	iface = net_if_get_first_by_type(&(NET_L2_GET_NAME(PPP)));
#else
#error Unknown LTE modem network interface
#endif
	/* Callbacks when states change */
	lte_control_state_cb.state_set = lte_control_state_set;
	lte_control_state_cb.state_cleared = lte_control_state_cleared;
	infuse_state_register_callback(&lte_control_state_cb);

	if (infuse_state_get(INFUSE_STATE_APPLICATION_ACTIVE)) {
		LOG_INF("Device active on boot, enable LTE");
		lte_control_request(LTE_REQUEST_CONNECT);
	}
}

#ifdef CONFIG_ZTEST

void auto_lte_control_test_cleanup(void)
{
	k_work_cancel(&lte_control_work);
	infuse_state_unregister_callback(&lte_control_state_cb);
	lte_control_state_cb.state_set = NULL;
	lte_control_state_cb.state_cleared = NULL;
	lte_control_state_cb.user_ctx = NULL;
	atomic_set(&pending_request, LTE_REQUEST_DISCONNECT);
	atomic_set(&last_request, LTE_REQUEST_DISCONNECT);
	iface = NULL;
}

#endif /* CONFIG_ZTEST*/

void auto_lte_control_give_up(void)
{
	if (atomic_get(&last_request) != LTE_REQUEST_CONNECT) {
		/* Connection not requested, no need to give up */
		LOG_DBG("Connection not requested, no need to give up");
		return;
	}
	if (lte_modem_monitor_is_registered()) {
		/* Modem is registered, no need to give up */
		LOG_DBG("Already registered, not giving up");
		return;
	}
	/* Application active, not registered, give up */
	LOG_INF("No connection, giving up");
	lte_control_request(LTE_REQUEST_DISCONNECT);
}

void auto_lte_control_retry(void)
{
	enum lte_request pending = atomic_get(&pending_request);
	enum lte_request last = atomic_get(&last_request);

	if (!infuse_state_get(INFUSE_STATE_APPLICATION_ACTIVE)) {
		/* Application is not active, don't attempt a connection */
		LOG_DBG("Not active, skipping retry");
		return;
	}
	if ((last != LTE_REQUEST_DISCONNECT) && (pending != LTE_REQUEST_DISCONNECT)) {
		/* Last request was not a disconnect, no need to retry */
		LOG_DBG("Not previously disconnected, skipping retry");
		return;
	}

	/* Application active, currently disconnected, try again */
	LOG_INF("Retrying connection");
	lte_control_request(LTE_REQUEST_CONNECT);
}
