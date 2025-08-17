/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log_output.h>

#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>

static const struct log_backend *log_backend_epacket_bt_get(void);

#define MAX_BUF                                                                                    \
	EPACKET_INTERFACE_MAX_PAYLOAD(DT_COMPAT_GET_ANY_STATUS_OKAY(embeint_epacket_bt_peripheral))

static uint8_t output_buf[MAX_BUF];
static uint32_t log_format_current = CONFIG_LOG_BACKEND_EPACKET_BT_OUTPUT_DEFAULT;
static bool panic_mode;

void epacket_bt_peripheral_logging_ccc_cfg_update(const struct bt_gatt_attr *attr, uint16_t value)
{
	bool enabled = value == BT_GATT_CCC_NOTIFY;
	static bool first_enable;

	if (enabled) {
		if (first_enable == false) {
			first_enable = true;
			log_backend_enable(log_backend_epacket_bt_get(), NULL,
					   CONFIG_LOG_MAX_LEVEL);
		} else {
			log_backend_activate(log_backend_epacket_bt_get(), NULL);
		}
	} else {
		log_backend_deactivate(log_backend_epacket_bt_get());
	}
}

static int line_out(uint8_t *data, size_t length, void *output_ctx)
{
	const struct device *dev = DEVICE_DT_GET_ONE(embeint_epacket_bt_peripheral);
	struct net_buf *buf = epacket_alloc_tx_for_interface(dev, K_NO_WAIT);

	ARG_UNUSED(output_ctx);

	/* Drop the line, no buffer */
	if (buf == NULL) {
		return length;
	}
	if (net_buf_tailroom(buf) == 0) {
		net_buf_unref(buf);
		return length;
	}

	epacket_set_tx_metadata(buf, EPACKET_AUTH_DEVICE, 0x00, INFUSE_SERIAL_LOG,
				EPACKET_ADDR_ALL);
	net_buf_add_mem(buf, data, MIN(length, net_buf_tailroom(buf)));
	epacket_queue(dev, buf);
	return length;
}

LOG_OUTPUT_DEFINE(log_output_epacket_bt, line_out, output_buf, sizeof(output_buf));

static void process(const struct log_backend *const backend, union log_msg_generic *msg)
{
	ARG_UNUSED(backend);
	uint32_t flags = LOG_OUTPUT_FLAG_FORMAT_SYSLOG | LOG_OUTPUT_FLAG_TIMESTAMP;

	if (panic_mode) {
		return;
	}

	log_format_func_t log_output_func = log_format_func_t_get(log_format_current);

	log_output_func(&log_output_epacket_bt, &msg->log, flags);
}

static void panic(struct log_backend const *const backend)
{
	ARG_UNUSED(backend);
	panic_mode = true;
}

static void init_ble(struct log_backend const *const backend)
{
	ARG_UNUSED(backend);
	log_backend_deactivate(log_backend_epacket_bt_get());
}

static int backend_ready(const struct log_backend *const backend)
{
	return -EACCES;
}

static int format_set(const struct log_backend *const backend, uint32_t log_type)
{
	ARG_UNUSED(backend);
	log_format_current = log_type;
	return 0;
}

const struct log_backend_api log_backend_epacket_bt_api = {
	.process = process,
	.panic = panic,
	.init = init_ble,
	.is_ready = backend_ready,
	.format_set = format_set,
};

LOG_BACKEND_DEFINE(log_backend_epacket_bt, log_backend_epacket_bt_api, true);

static const struct log_backend *log_backend_epacket_bt_get(void)
{
	return &log_backend_epacket_bt;
}
