/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>

#include <eis/epacket/interface.h>
#include <eis/epacket/packet.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

int main(void)
{
	const struct device *epacket_usb = DEVICE_DT_GET(DT_NODELABEL(epacket_usb));
	struct net_buf *buf;
	int ret, cnt = 0;

	ret = usb_enable(NULL);
	if (ret != 0) {
		LOG_ERR("usb enable error %d", ret);
	}

	for (;;) {
		buf = epacket_alloc_tx_for_interface(epacket_usb, K_FOREVER);

		net_buf_add_le32(buf, cnt);

		epacket_queue(epacket_usb, buf);
		LOG_INF("Sent %s %d %d", epacket_usb->name, ret, cnt++);
		k_sleep(K_SECONDS(1));
	}
	return 0;
}
