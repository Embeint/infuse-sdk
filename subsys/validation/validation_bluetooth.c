/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/bluetooth/bluetooth.h>

#include <infuse/epacket/packet.h>
#include <infuse/validation/core.h>
#include <infuse/validation/bluetooth.h>

#define TEST "BT"

static K_SEM_DEFINE(tx_done, 0, 1);
static int send_rc;

static void tx_done_cb(const struct device *dev, struct net_buf *buf, int result, void *user_data)
{
	send_rc = result;
	k_sem_give(&tx_done);
}

int infuse_validation_bluetooth(uint8_t flags)
{
	const struct device *dev =
		DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(embeint_epacket_bt_adv));
	struct net_buf *pkt;
	int rc;

	VALIDATION_REPORT_INFO(TEST, "START");

	/* Synchronously enable the Bluetooth system */
	rc = bt_enable(NULL);
	if (rc) {
		VALIDATION_REPORT_ERROR(TEST, "Bluetooth enable failed (%d)", rc);
		goto test_end;
	}

	if (flags & VALIDATION_BLUETOOTH_ADV_TX) {
		pkt = epacket_alloc_tx_for_interface(dev, K_FOREVER);
		epacket_set_tx_metadata(pkt, EPACKET_AUTH_NETWORK, 0, INFUSE_ECHO_REQ,
					EPACKET_ADDR_ALL);
		epacket_set_tx_callback(pkt, tx_done_cb, NULL);
		net_buf_add_mem(pkt, "HELLO", 5);
		epacket_queue(dev, pkt);

		if (k_sem_take(&tx_done, K_SECONDS(1)) != 0) {
			VALIDATION_REPORT_ERROR(TEST, "Advertising TX timeout");
			goto test_end;
		}

		rc = send_rc;
		if (send_rc == 0) {
			VALIDATION_REPORT_INFO(TEST, "Advertising TX succeeded");
		} else {
			VALIDATION_REPORT_ERROR(TEST, "Advertising TX failed (%d)", send_rc);
		}
	}

test_end:
	if (rc == 0) {
		VALIDATION_REPORT_PASS(TEST, "PASSED");
	}
	return rc;
}
