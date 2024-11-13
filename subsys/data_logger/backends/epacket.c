/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#define DT_DRV_COMPAT embeint_data_logger_epacket

#include <zephyr/device.h>

#include <infuse/epacket/packet.h>
#include <infuse/data_logger/logger.h>

#include "common.h"

struct dl_epacket_config {
	struct data_logger_common_config common;
	const struct device *backend;
	uint16_t max_block_size;
};
struct dl_epacket_data {
	struct data_logger_common_data common;
	struct epacket_interface_cb interface_cb;
};

static int logger_epacket_write(const struct device *dev, uint32_t phy_block,
				enum infuse_type data_type, const void *mem, uint16_t mem_len)
{
	const struct dl_epacket_config *config = dev->config;
	struct net_buf *buf = epacket_alloc_tx_for_interface(config->backend, K_FOREVER);

	if (net_buf_tailroom(buf) < mem_len) {
		net_buf_unref(buf);
		return -ENOSPC;
	}
	epacket_set_tx_metadata(buf, EPACKET_AUTH_NETWORK, 0x00, data_type);
	net_buf_add_mem(buf, mem, mem_len);
	epacket_queue(config->backend, buf);
	return 0;
}

static void epacket_interface_state(uint16_t current_max_payload, void *user_ctx)
{
	const struct device *dev = user_ctx;

	data_logger_common_block_size_changed(dev, current_max_payload);
}

/* Need to hook into this function when testing */
IF_DISABLED(CONFIG_ZTEST, (static))
int logger_epacket_init(const struct device *dev)
{
	const struct dl_epacket_config *config = dev->config;
	struct dl_epacket_data *data = dev->data;

	/* Setup common data structure */
	data->common.physical_blocks = UINT32_MAX;
	data->common.logical_blocks = UINT32_MAX;
	data->common.block_size = config->max_block_size;

	/* Register for callbacks on state changes */
	data->interface_cb.interface_state = epacket_interface_state;
	data->interface_cb.user_ctx = (void *)dev;
	epacket_register_callback(config->backend, &data->interface_cb);

	return data_logger_common_init(dev);
}

const struct data_logger_api data_logger_epacket_api = {
	.write = logger_epacket_write,
};

#define DATA_LOGGER_DEFINE(inst)                                                                   \
	COMMON_CONFIG_PRE(inst);                                                                   \
	static struct dl_epacket_config config##inst = {                                           \
		.common = COMMON_CONFIG_INIT(inst, false, true),                                   \
		.backend = DEVICE_DT_GET(DT_INST_PROP(inst, epacket)),                             \
		.max_block_size = EPACKET_INTERFACE_MAX_PAYLOAD(DT_INST_PROP(inst, epacket)),      \
	};                                                                                         \
	static struct dl_epacket_data data##inst;                                                  \
	DEVICE_DT_INST_DEFINE(inst, logger_epacket_init, NULL, &data##inst, &config##inst,         \
			      POST_KERNEL, 80, &data_logger_epacket_api);

#define DATA_LOGGER_DEFINE_WRAPPER(inst)                                                           \
	IF_ENABLED(DATA_LOGGER_DEPENDENCIES_MET(DT_DRV_INST(inst)), (DATA_LOGGER_DEFINE(inst)))

DT_INST_FOREACH_STATUS_OKAY(DATA_LOGGER_DEFINE_WRAPPER)
