/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/sys/atomic.h>

#include <infuse/reboot.h>
#include <infuse/lib/lte_modem_monitor.h>

#include "modem_monitor.h"

LOG_MODULE_REGISTER(modem_monitor, CONFIG_INFUSE_MODEM_MONITOR_LOG_LEVEL);

#define CONNECTIVITY_TIMEOUT K_SECONDS(CONFIG_INFUSE_MODEM_MONITOR_CONNECTIVITY_TIMEOUT_SEC)
#define DNS_FAILURE_TIMEOUT  K_SECONDS(CONFIG_INFUSE_MODEM_MONITOR_DNS_FAILURE_TIMEOUT_SEC)

enum {
	FLAGS_IP_CONN_EXPECTED = 0,
	FLAGS_L4_CONNECTED = 1,
};

static struct {
	struct net_if *net_if;
	struct net_mgmt_event_callback mgmt_iface_cb;
	struct k_work_delayable connectivity_timeout;
	struct k_work_delayable dns_failure_timeout;
	atomic_t flags;
} generic_monitor;

static void connectivity_failed(uint32_t info1, uint32_t info2)
{
	/* The safest option is to reboot */
#ifdef CONFIG_INFUSE_REBOOT
	LOG_ERR("Networking connectivity failed, rebooting in 2 seconds...");
	infuse_reboot_delayed(INFUSE_REBOOT_SW_WATCHDOG, info1, info2, K_SECONDS(2));
#else
	LOG_ERR("Networking connectivity failed, no reboot support!");
#endif /* CONFIG_INFUSE_REBOOT */
}

static void connectivity_timeout(struct k_work *work)
{
	if (!atomic_test_bit(&generic_monitor.flags, FLAGS_IP_CONN_EXPECTED)) {
		/* Network registration was lost before interface state callback occurred */
		LOG_DBG("Timeout expired but connectivity not expected");
		return;
	}
	if (atomic_test_bit(&generic_monitor.flags, FLAGS_L4_CONNECTED)) {
		/* Interface connected before the timeout could be cancelled */
		LOG_DBG("Timeout expired but interface connected");
		return;
	}

	/* Interface has failed to gain IP connectivity */
	connectivity_failed((uintptr_t)connectivity_timeout,
			    CONFIG_INFUSE_MODEM_MONITOR_CONNECTIVITY_TIMEOUT_SEC);
}

static void dns_failure_timeout(struct k_work *work)
{
	if (!atomic_test_bit(&generic_monitor.flags, FLAGS_IP_CONN_EXPECTED) ||
	    !atomic_test_bit(&generic_monitor.flags, FLAGS_L4_CONNECTED)) {
		/* Connectivity was lost before the timeout was cancelled */
		LOG_DBG("DNS timeout expired but connectivity not expected");
		return;
	}

	/* IP connectivity reported, but DNS queries are not being answered */
	LOG_ERR("No DNS answers for %d seconds",
		CONFIG_INFUSE_MODEM_MONITOR_DNS_FAILURE_TIMEOUT_SEC);
	connectivity_failed((uintptr_t)dns_failure_timeout,
			    CONFIG_INFUSE_MODEM_MONITOR_DNS_FAILURE_TIMEOUT_SEC);
}

static void l4_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			     struct net_if *iface)
{
	if (iface != generic_monitor.net_if) {
		return;
	}
	if (mgmt_event == NET_EVENT_L4_IF_CONNECTED) {
		/* Interface is connected, cancel the timeout */
		LOG_DBG("Interface %p is connected, cancelling timeout", iface);
		atomic_set_bit(&generic_monitor.flags, FLAGS_L4_CONNECTED);
		k_work_cancel_delayable(&generic_monitor.connectivity_timeout);
	} else if (mgmt_event == NET_EVENT_L4_IF_DISCONNECTED) {
		/* Interface is disconnected, restart the timeout */
		LOG_DBG("Interface %p is disconnected, restarting timeout", iface);
		atomic_clear_bit(&generic_monitor.flags, FLAGS_L4_CONNECTED);
		k_work_cancel_delayable(&generic_monitor.dns_failure_timeout);
		k_work_reschedule(&generic_monitor.connectivity_timeout, CONNECTIVITY_TIMEOUT);
	}
}

void modem_monitor_ip_connectivity_expected(bool expected)
{
	if (expected) {
		LOG_DBG("IP connectivity expected");
		atomic_set_bit(&generic_monitor.flags, FLAGS_IP_CONN_EXPECTED);
		if (atomic_test_bit(&generic_monitor.flags, FLAGS_L4_CONNECTED)) {
			/* Interface stayed up while registration was briefly lost, no new
			 * L4_IF_CONNECTED event will arrive to cancel the timeout.
			 */
			LOG_DBG("Interface already connected");
			return;
		}
		k_work_reschedule(&generic_monitor.connectivity_timeout, CONNECTIVITY_TIMEOUT);
	} else {
		LOG_DBG("IP connectivity not expected");
		atomic_clear_bit(&generic_monitor.flags, FLAGS_IP_CONN_EXPECTED);
		k_work_cancel_delayable(&generic_monitor.connectivity_timeout);
		k_work_cancel_delayable(&generic_monitor.dns_failure_timeout);
	}
}

void lte_modem_monitor_dns_result(bool answered)
{
	if ((CONFIG_INFUSE_MODEM_MONITOR_DNS_FAILURE_TIMEOUT_SEC == 0) ||
	    (generic_monitor.net_if == NULL)) {
		/* Timeout disabled or monitor not yet initialised */
		return;
	}

	if (answered) {
		/* Data path through the network is working */
		k_work_cancel_delayable(&generic_monitor.dns_failure_timeout);
		return;
	}

	if (!atomic_test_bit(&generic_monitor.flags, FLAGS_IP_CONN_EXPECTED) ||
	    !atomic_test_bit(&generic_monitor.flags, FLAGS_L4_CONNECTED)) {
		/* Failure is expected without IP connectivity */
		return;
	}

	/* Start the timeout if not already running, later failures don't extend it */
	if (k_work_schedule(&generic_monitor.dns_failure_timeout, DNS_FAILURE_TIMEOUT) == 1) {
		LOG_WRN("DNS query failed, starting %d second timeout",
			CONFIG_INFUSE_MODEM_MONITOR_DNS_FAILURE_TIMEOUT_SEC);
	}
}

void modem_monitor_init(struct net_if *iface)
{
	__ASSERT_NO_MSG(iface != NULL);
	generic_monitor.net_if = iface;
	k_work_init_delayable(&generic_monitor.connectivity_timeout, connectivity_timeout);
	k_work_init_delayable(&generic_monitor.dns_failure_timeout, dns_failure_timeout);
	net_mgmt_init_event_callback(&generic_monitor.mgmt_iface_cb, l4_event_handler,
				     NET_EVENT_L4_IF_CONNECTED | NET_EVENT_L4_IF_DISCONNECTED);
	net_mgmt_add_event_callback(&generic_monitor.mgmt_iface_cb);
}
