/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/util.h>

#include <zephyr/device.h>
#include <infuse/validation/core.h>
#include <infuse/validation/gnss.h>

#include <infuse/modem/backend/u_blox_i2c.h>
#include <infuse/gnss/ubx/defines.h>
#include <infuse/gnss/ubx/cfg.h>
#include <infuse/gnss/ubx/modem.h>
#include <infuse/gnss/ubx/protocol.h>
#include <infuse/gnss/ubx/zephyr.h>

#define TEST                 "GNSS"
#define SYNC_MESSAGE_TIMEOUT K_MSEC(250)

static int mon_ver_handler(uint8_t message_class, uint8_t message_id, const void *payload,
			   size_t payload_len, void *user_data)
{
	const struct ubx_msg_mon_ver *ver = payload;
	uint8_t num_ext = (payload_len - sizeof(*ver)) / 30;

	VALIDATION_REPORT_INFO(TEST, "    SW: %s", ver->sw_version);
	VALIDATION_REPORT_INFO(TEST, "    HW: %s", ver->hw_version);
	for (int i = 0; i < num_ext; i++) {
		VALIDATION_REPORT_INFO(TEST, " EXT %d: %s", i, ver->extension[i].ext_version);
	}
	return 0;
}

#ifdef CONFIG_GNSS_UBX_M10

static int sec_uniqid_handler(uint8_t message_class, uint8_t message_id, const void *payload,
			      size_t payload_len, void *user_data)
{
	const struct ubx_msg_sec_uniqid *uniqid = payload;
	const uint8_t *id = uniqid->uniqueId;

	VALIDATION_REPORT_INFO(TEST, "UNIQID: %02X%02X%02X%02X%02X%02X", id[0], id[1], id[2], id[3],
			       id[4], id[5]);
	return 0;
}

#endif /* CONFIG_GNSS_UBX_M10 */

#ifdef CONFIG_GNSS_UBX_M8
static int ubx_m8_dcdc_burn(struct ubx_modem_data *modem)
{
	/* ZOE-M8 Integration Guide: section 2.1.3 */
	static const uint8_t cfg_val[] = {0x00, 0x00, 0x03, 0x1F, 0xC5, 0x90,
					  0xE1, 0x9F, 0xFF, 0xFF, 0xFE, 0xFF};
	NET_BUF_SIMPLE_DEFINE(buf, 32);

	ubx_msg_prepare(&buf, UBX_MSG_CLASS_CFG, UBX_MSG_ID_CFG_DCDC_BURN);
	net_buf_simple_add_mem(&buf, cfg_val, sizeof(cfg_val));
	ubx_msg_finalise(&buf);

	return ubx_modem_send_sync_acked(modem, &buf, K_SECONDS(2));
}
#endif /* CONFIG_GNSS_UBX_M8 */

int infuse_validation_gnss(const struct device *dev, uint8_t flags)
{
	struct ubx_modem_data *modem = ubx_modem_data_get(dev);
	int rc;

	VALIDATION_REPORT_INFO(TEST, "DEV=%s", dev->name);

	/* Check init succeeded */
	if (!device_is_ready(dev)) {
		VALIDATION_REPORT_ERROR(TEST, "Device not ready");
		rc = -ENODEV;
		goto test_end;
	}

	/* Power up device */
	rc = pm_device_runtime_get(dev);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "pm_device_runtime_get (%d)", rc);
		goto test_end;
	}

	/* Query and display system version information */
	rc = ubx_modem_send_sync_poll(modem, UBX_MSG_CLASS_MON, UBX_MSG_ID_MON_VER, mon_ver_handler,
				      NULL, SYNC_MESSAGE_TIMEOUT);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to query MON-VER (%d)", rc);
		goto power_down;
	}

#ifdef CONFIG_GNSS_UBX_M10
	/* Query and display system version information */
	rc = ubx_modem_send_sync_poll(modem, UBX_MSG_CLASS_SEC, UBX_MSG_ID_SEC_UNIQID,
				      sec_uniqid_handler, NULL, SYNC_MESSAGE_TIMEOUT);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to query SEC-UNIQID (%d)", rc);
		goto power_down;
	}
#endif /* CONFIG_GNSS_UBX_M10 */

#ifdef CONFIG_GNSS_UBX_M8
	if (flags & VALIDATION_GNSS_UBX_M8_DC_DC_BURN) {
		VALIDATION_REPORT_INFO(TEST, "Permanently enabling DC-DC converter");
		rc = ubx_m8_dcdc_burn(modem);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "Failed to enable DC-DC converter (%d)", rc);
			goto power_down;
		}
		VALIDATION_REPORT_INFO(TEST, "DC-DC converter permanently enabled");
	}
#endif /* CONFIG_GNSS_UBX_M8 */

power_down:
	/* Power down device */
	if (pm_device_runtime_put(dev) < 0) {
		if (rc == 0) {
			VALIDATION_REPORT_ERROR(TEST, "pm_device_runtime_put");
			rc = -EIO;
		}
	}
test_end:
	if (rc == 0) {
		VALIDATION_REPORT_PASS(TEST, "DEV=%s", dev->name);
	}

	return rc;
}
