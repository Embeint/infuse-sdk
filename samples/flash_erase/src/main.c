/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>

#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

bool callback(const struct flash_pages_info *info, void *data)
{
	uint32_t *flash_size = data;

	*flash_size += info->size;
	return true;
}

static void do_flash_erase(const struct device *dev)
{
	uint32_t flash_size = 0;
	int rc;

	if (!device_is_ready(dev)) {
		LOG_ERR("Device %s is not ready!", dev->name);
		return;
	}

	/* Determine size of flash device */
	flash_page_foreach(dev, callback, &flash_size);

	/* Erase flash device */
	LOG_INF("Erasing device %s (%u bytes)...", dev->name, flash_size);
	rc = flash_erase(dev, 0, flash_size);
	if (rc == 0) {
		LOG_INF("Device %s erased successfully", dev->name);
	} else {
		LOG_INF("Failed to erase %s (%d)", dev->name, rc);
	}
}

int main(void)
{
	const struct device *spi_nor = DEVICE_DT_GET_ANY(jedec_spi_nor);
	const struct device *qspi_nor = DEVICE_DT_GET_ANY(nordic_qspi_nor);

	if (spi_nor) {
		do_flash_erase(spi_nor);
	}
	if (qspi_nor) {
		do_flash_erase(qspi_nor);
	}

	LOG_INF("Application complete");
	return 0;
}
