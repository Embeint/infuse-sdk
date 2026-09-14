/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usbd.h>

LOG_MODULE_REGISTER(infuse_cdc_acm_serial, CONFIG_USBD_LOG_LEVEL);

USBD_DEVICE_DEFINE(infuse_cdc_acm_serial, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
		   CONFIG_INFUSE_CDC_ACM_SERIAL_VID, CONFIG_INFUSE_CDC_ACM_SERIAL_PID);

USBD_DESC_LANG_DEFINE(infuse_cdc_acm_serial_lang);
USBD_DESC_MANUFACTURER_DEFINE(infuse_cdc_acm_serial_mfr,
			      CONFIG_INFUSE_CDC_ACM_SERIAL_MANUFACTURER_STRING);
USBD_DESC_PRODUCT_DEFINE(infuse_cdc_acm_serial_product,
			 CONFIG_INFUSE_CDC_ACM_SERIAL_PRODUCT_STRING);
IF_ENABLED(CONFIG_HWINFO, (USBD_DESC_SERIAL_NUMBER_DEFINE(infuse_cdc_acm_serial_sn)));

USBD_DESC_CONFIG_DEFINE(fs_cfg_desc, "FS Configuration");
USBD_DESC_CONFIG_DEFINE(hs_cfg_desc, "HS Configuration");

static const uint8_t attributes =
	IS_ENABLED(CONFIG_INFUSE_CDC_ACM_SERIAL_SELF_POWERED) ? USB_SCD_SELF_POWERED : 0;

USBD_CONFIGURATION_DEFINE(infuse_cdc_acm_serial_fs_config, attributes,
			  CONFIG_INFUSE_CDC_ACM_SERIAL_MAX_POWER, &fs_cfg_desc);

USBD_CONFIGURATION_DEFINE(infuse_cdc_acm_serial_hs_config, attributes,
			  CONFIG_INFUSE_CDC_ACM_SERIAL_MAX_POWER, &hs_cfg_desc);

static void infuse_cdc_acm_msg_handler(struct usbd_context *const uds_ctx,
				       const struct usbd_msg *msg)
{
	ARG_UNUSED(uds_ctx);

	LOG_DBG("USBD message: %s", usbd_msg_type_string(msg->type));

	if (msg->type == USBD_MSG_CDC_ACM_CONTROL_LINE_STATE) {
		uint32_t dtr = 0;
		int rc;

		rc = uart_line_ctrl_get(msg->dev, UART_LINE_CTRL_DTR, &dtr);
		if (rc == 0) {
			LOG_DBG("CDC ACM DTR %s", dtr ? "set" : "cleared");
		}
	}
}

static int register_cdc_acm_0(struct usbd_context *const uds_ctx, const enum usbd_speed speed)
{
	struct usbd_config_node *cfg_nd;
	int err;

	if (speed == USBD_SPEED_HS) {
		cfg_nd = &infuse_cdc_acm_serial_hs_config;
	} else {
		cfg_nd = &infuse_cdc_acm_serial_fs_config;
	}

	err = usbd_add_configuration(uds_ctx, speed, cfg_nd);
	if (err) {
		LOG_ERR("Failed to add configuration");
		return err;
	}

	err = usbd_register_class(&infuse_cdc_acm_serial, "cdc_acm_0", speed, 1);
	if (err) {
		LOG_ERR("Failed to register CDC ACM class");
		return err;
	}

	return usbd_device_set_code_triple(uds_ctx, speed, USB_BCC_MISCELLANEOUS, 0x02, 0x01);
}

static int infuse_cdc_acm_serial_init(void)
{
	int err;

	err = usbd_add_descriptor(&infuse_cdc_acm_serial, &infuse_cdc_acm_serial_lang);
	if (err) {
		LOG_ERR("Failed to initialize language descriptor (%d)", err);
		return err;
	}

	err = usbd_add_descriptor(&infuse_cdc_acm_serial, &infuse_cdc_acm_serial_mfr);
	if (err) {
		LOG_ERR("Failed to initialize manufacturer descriptor (%d)", err);
		return err;
	}

	err = usbd_add_descriptor(&infuse_cdc_acm_serial, &infuse_cdc_acm_serial_product);
	if (err) {
		LOG_ERR("Failed to initialize product descriptor (%d)", err);
		return err;
	}

	IF_ENABLED(CONFIG_HWINFO,
		   (err = usbd_add_descriptor(&infuse_cdc_acm_serial, &infuse_cdc_acm_serial_sn);))
	if (err) {
		LOG_ERR("Failed to initialize serial number descriptor (%d)", err);
		return err;
	}

	if (USBD_SUPPORTS_HIGH_SPEED && usbd_caps_speed(&infuse_cdc_acm_serial) == USBD_SPEED_HS) {
		err = register_cdc_acm_0(&infuse_cdc_acm_serial, USBD_SPEED_HS);
		if (err) {
			return err;
		}
	}

	err = register_cdc_acm_0(&infuse_cdc_acm_serial, USBD_SPEED_FS);
	if (err) {
		return err;
	}

	err = usbd_msg_register_cb(&infuse_cdc_acm_serial, infuse_cdc_acm_msg_handler);
	if (err) {
		LOG_ERR("Failed to register message handler (%d)", err);
		return err;
	}

	err = usbd_init(&infuse_cdc_acm_serial);
	if (err) {
		LOG_ERR("Failed to initialize USB device support (%d)", err);
		return err;
	}

	if (IS_ENABLED(CONFIG_INFUSE_CDC_ACM_SERIAL_ENABLE_AT_BOOT)) {
		err = usbd_enable(&infuse_cdc_acm_serial);
		if (err) {
			LOG_ERR("Failed to enable USB device support (%d)", err);
			return err;
		}
	}

	return 0;
}

SYS_INIT(infuse_cdc_acm_serial_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
