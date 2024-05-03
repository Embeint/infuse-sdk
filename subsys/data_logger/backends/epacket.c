/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#define DT_DRV_COMPAT embeint_data_logger_epacket

#include <zephyr/device.h>

#include <infuse/epacket/packet.h>

#include "backend_api.h"
#include "epacket.h"

static int logger_epacket_write(const struct data_logger_backend_config *backend, uint32_t phy_block,
				enum infuse_type data_type, const void *mem, uint16_t mem_len)
{
	struct net_buf *buf = epacket_alloc_tx_for_interface(backend->backend, K_FOREVER);

	epacket_set_tx_metadata(buf, EPACKET_AUTH_NETWORK, 0x00, data_type);
	net_buf_add_mem(buf, mem, mem_len);
	epacket_queue(backend->backend, buf);
	return 0;
}

static int logger_epacket_init(const struct data_logger_backend_config *backend)
{
	struct data_logger_backend_data *data = backend->data;

	/* Fixed block size */
	data->block_size = backend->max_block_size;
	return 0;
}

const struct data_logger_backend_api data_logger_epacket_api = {
	.init = logger_epacket_init,
	.write = logger_epacket_write,
};
