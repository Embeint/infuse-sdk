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

#include "modem_monitor.h"

LOG_MODULE_REGISTER(modem_monitor, CONFIG_INFUSE_MODEM_MONITOR_LOG_LEVEL);

#define CONNECTIVITY_TIMEOUT K_SECONDS(CONFIG_INFUSE_MODEM_MONITOR_CONNECTIVITY_TIMEOUT_SEC)

enum {
	FLAGS_IP_CONN_EXPECTED = 0,
};

static struct {
	struct net_if *net_if;
	struct net_mgmt_event_callback mgmt_iface_cb;
	struct k_work_delayable connectivity_timeout;
	atomic_t flags;
} generic_monitor;

static void connectivity_timeout(struct k_work *work)
{
	if (!atomic_test_bit(&generic_monitor.flags, FLAGS_IP_CONN_EXPECTED)) {
		/* Network registration was lost before interface state callback occurred */
		return;
	}

	/* Interface has failed to gain IP connectivity, the safest option is to reboot */
#ifdef CONFIG_INFUSE_REBOOT
	LOG_ERR("Networking connectivity failed, rebooting in 2 seconds...");
	infuse_reboot_delayed(INFUSE_REBOOT_SW_WATCHDOG, (uintptr_t)connectivity_timeout,
			      CONFIG_INFUSE_MODEM_MONITOR_CONNECTIVITY_TIMEOUT_SEC, K_SECONDS(2));
#else
	LOG_ERR("Networking connectivity failed, no reboot support!");
#endif /* CONFIG_INFUSE_REBOOT */
}

static void iface_state_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
				struct net_if *iface)
{
	if (iface != generic_monitor.net_if) {
		return;
	}
	if (mgmt_event == NET_EVENT_IF_UP) {
		/* Interface is UP, cancel the timeout */
		k_work_cancel_delayable(&generic_monitor.connectivity_timeout);
	} else if (mgmt_event == NET_EVENT_IF_DOWN) {
		/* Interface is DOWN, restart the timeout */
		k_work_reschedule(&generic_monitor.connectivity_timeout, CONNECTIVITY_TIMEOUT);
	}
}

void modem_monitor_ip_connectivity_expected(bool expected)
{
	if (expected) {
		atomic_set_bit(&generic_monitor.flags, FLAGS_IP_CONN_EXPECTED);
		k_work_reschedule(&generic_monitor.connectivity_timeout, CONNECTIVITY_TIMEOUT);
	} else {
		atomic_clear_bit(&generic_monitor.flags, FLAGS_IP_CONN_EXPECTED);
		k_work_cancel_delayable(&generic_monitor.connectivity_timeout);
	}
}

void modem_monitor_init(struct net_if *iface)
{
	__ASSERT_NO_MSG(iface != NULL);
	generic_monitor.net_if = iface;
	k_work_init_delayable(&generic_monitor.connectivity_timeout, connectivity_timeout);
	net_mgmt_init_event_callback(&generic_monitor.mgmt_iface_cb, iface_state_handler,
				     NET_EVENT_IF_UP | NET_EVENT_IF_DOWN);
	net_mgmt_add_event_callback(&generic_monitor.mgmt_iface_cb);
}
