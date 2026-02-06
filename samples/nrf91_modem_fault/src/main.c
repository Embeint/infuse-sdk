/**
 * @file
 * @copyright 2026 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/conn_mgr_connectivity.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("Enable LTE for first time");
	conn_mgr_all_if_up(false);
	conn_mgr_all_if_connect(false);

	LOG_INF("Waiting 15 seconds to enter shipping mode...");
	k_sleep(K_SECONDS(15));

	LOG_INF("Bring down LTE for shipping mode");
	conn_mgr_all_if_disconnect(false);
	conn_mgr_all_if_down(false);

	int countdown = 65;
	while (countdown > 0) {
		LOG_INF("%d seconds to exit shipping mode", countdown);
		k_sleep(K_SECONDS(5));
		countdown -= 5;
	}

	LOG_INF("Exiting shipping mode");
	conn_mgr_all_if_up(false);
	conn_mgr_all_if_connect(false);

	k_sleep(K_FOREVER);
}
