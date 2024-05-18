/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

int main(void)
{
	const struct device *tdf_logger_serial = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_serial));
	struct tdf_announce announce;

	for (;;) {
		announce.application = 0x1234;
		announce.reboots = 0;
		announce.uptime = k_uptime_get() / 1000;
		announce.version = (struct tdf_struct_mcuboot_img_sem_ver){
			.major = 1,
			.minor = 2,
			.revision = 3,
			.build_num = 4,
		};

		tdf_data_logger_log_dev(tdf_logger_serial, TDF_ANNOUNCE, (sizeof(announce)), 0, &announce);
		tdf_data_logger_flush_dev(tdf_logger_serial);

		LOG_INF("Sent uptime %d on %s", announce.uptime, tdf_logger_serial->name);
		k_sleep(K_SECONDS(1));
	}
	return 0;
}
