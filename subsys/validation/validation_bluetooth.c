/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/bluetooth/bluetooth.h>

#include <infuse/bluetooth/controller_manager.h>
#include <infuse/epacket/packet.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/validation/core.h>
#include <infuse/validation/bluetooth.h>

#define TEST "BT"

#define EPACKET_BT_ADV_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(embeint_epacket_bt_adv)

/* Duration to transmit legacy advertising packets for */
#define LEGACY_ADV_DURATION K_MSEC(500)

#if DT_NODE_EXISTS(EPACKET_BT_ADV_NODE)

static K_SEM_DEFINE(tx_done, 0, 1);
static int send_rc;

static void tx_done_cb(const struct device *dev, struct net_buf *buf, int result, void *user_data)
{
	send_rc = result;
	k_sem_give(&tx_done);
}

static int validation_adv_tx(void)
{
	const struct device *dev = DEVICE_DT_GET(EPACKET_BT_ADV_NODE);
	struct net_buf *pkt;

	pkt = epacket_alloc_tx_for_interface(dev, K_FOREVER);
	epacket_set_tx_metadata(pkt, EPACKET_AUTH_NETWORK, 0, INFUSE_ECHO_REQ, EPACKET_ADDR_ALL);
	epacket_set_tx_callback(pkt, tx_done_cb, NULL);
	net_buf_add_mem(pkt, "HELLO", 5);
	epacket_queue(dev, pkt);

	if (k_sem_take(&tx_done, K_SECONDS(1)) != 0) {
		VALIDATION_REPORT_ERROR(TEST, "Advertising TX timeout");
		return -ETIMEDOUT;
	}
	if (send_rc != 0) {
		VALIDATION_REPORT_ERROR(TEST, "Advertising TX failed (%d)", send_rc);
		return send_rc;
	}
	VALIDATION_REPORT_INFO(TEST, "Advertising TX succeeded");
	return 0;
}

#else

static int validation_adv_tx(void)
{
	VALIDATION_REPORT_ERROR(TEST, "No ePacket advertising interface");
	return -ENOTSUP;
}

#endif /* DT_NODE_EXISTS(EPACKET_BT_ADV_NODE) */

static int validation_legacy_adv_tx(void)
{
	const struct bt_data ad[] = {
		BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
		BT_DATA_BYTES(BT_DATA_MANUFACTURER_DATA, 'H', 'E', 'L', 'L', 'O'),
	};
	const struct bt_le_adv_param param =
		BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_USE_IDENTITY, BT_GAP_ADV_FAST_INT_MIN_1,
				     BT_GAP_ADV_FAST_INT_MAX_1, NULL);
	int rc;

	/* Legacy advertising has no completion callback, so validate that the controller
	 * accepts the advertising data and transmits for a short duration.
	 */
	rc = bt_le_adv_start(&param, ad, ARRAY_SIZE(ad), NULL, 0);
	if (rc != 0) {
		VALIDATION_REPORT_ERROR(TEST, "Legacy advertising start failed (%d)", rc);
		return rc;
	}
	k_sleep(LEGACY_ADV_DURATION);
	rc = bt_le_adv_stop();
	if (rc != 0) {
		VALIDATION_REPORT_ERROR(TEST, "Legacy advertising stop failed (%d)", rc);
		return rc;
	}
	VALIDATION_REPORT_INFO(TEST, "Legacy advertising TX succeeded");
	return 0;
}

int infuse_validation_bluetooth(uint8_t flags)
{
	int rc;

	VALIDATION_REPORT_INFO(TEST, "START");

	/* Synchronously enable the Bluetooth system */
	rc = bt_enable(NULL);
	if (rc) {
		VALIDATION_REPORT_ERROR(TEST, "Bluetooth enable failed (%d)", rc);
		goto test_end;
	}

#ifdef CONFIG_BT_CONTROLLER_MANAGER
	rc = bt_controller_manager_init();
	if (rc) {
		VALIDATION_REPORT_ERROR(TEST, "Bluetooth Controller Manager failed (%d)", rc);
		goto test_end;
	}
#ifdef CONFIG_KV_STORE_KEY_BLUETOOTH_CTLR_VERSION
	KV_KEY_TYPE(KV_KEY_BLUETOOTH_CTLR_VERSION) bt_ctlr_ver;

	if (KV_STORE_READ(KV_KEY_BLUETOOTH_CTLR_VERSION, &bt_ctlr_ver) > 0) {
		VALIDATION_REPORT_VALUE(TEST, "CTLR_VERSION", "%d.%d.%d+%08x",
					bt_ctlr_ver.version.major, bt_ctlr_ver.version.minor,
					bt_ctlr_ver.version.revision,
					bt_ctlr_ver.version.build_num);
	}
#endif /* CONFIG_KV_STORE_KEY_BLUETOOTH_CTLR_VERSION */
#endif /* CONFIG_BT_CONTROLLER_MANAGE*/

	if (flags & VALIDATION_BLUETOOTH_ADV_TX) {
		rc = validation_adv_tx();
		if (rc != 0) {
			goto test_end;
		}
	}

	if (flags & VALIDATION_BLUETOOTH_LEGACY_ADV_TX) {
		rc = validation_legacy_adv_tx();
		if (rc != 0) {
			goto test_end;
		}
	}

test_end:
	if (rc == 0) {
		VALIDATION_REPORT_PASS(TEST, "PASSED");
	}
	return rc;
}
