/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/conn_mgr_connectivity.h>

#include <infuse/time/civil.h>
#include <infuse/tdf/definitions.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/drivers/watchdog.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

int main(void)
{
	const struct device *tdf_logger_udp = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_udp));
	struct tdf_announce announce;

	/* Start watchdog */
	infuse_watchdog_start();

	k_sleep(K_SECONDS(2));

	/* Turn on the interface */
	conn_mgr_all_if_up(false);
	conn_mgr_all_if_connect(false);

	while (true) {
		k_sleep(K_SECONDS(60));

		/* Push announce packet once a minute over UDP */
		announce.application = 0x1234;
		announce.reboots = 0;
		announce.uptime = k_uptime_get() / 1000;
		announce.version = (struct tdf_struct_mcuboot_img_sem_ver){
			.major = 1,
			.minor = 2,
			.revision = 3,
			.build_num = 4,
		};

		tdf_data_logger_log_dev(tdf_logger_udp, TDF_ANNOUNCE, (sizeof(announce)), 0,
					&announce);
		tdf_data_logger_flush_dev(tdf_logger_udp);
	}
}
