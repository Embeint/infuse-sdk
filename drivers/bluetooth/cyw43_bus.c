/* Copyright (c) 2026 Embeint Holdings Pty Ltd
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <whd_chip.h>
#include <whd_chip_reg.h>
#include <whd_bus_common.h>
#include <whd_bus_protocol_interface.h>
#include <whd_bus_spi_protocol.h>

#include "cyw43_bus.h"

static K_MUTEX_DEFINE(bus_mutex);

void cyw43_bus_lock(void)
{
	k_mutex_lock(&bus_mutex, K_FOREVER);
}

void cyw43_bus_unlock(void)
{
	k_mutex_unlock(&bus_mutex);
}

#define BACKPLANE_OFFSET_MASK 0x7fffU
#define BT_POWER_CONTROL      0x19640894U

static bool register_address(uint32_t address)
{
	return (address >= CHIPCOMMON_BASE_ADDRESS && address < CHIPCOMMON_BASE_ADDRESS + 0x8000) ||
	       address == BT_POWER_CONTROL;
}

static whd_result_t word_read(whd_driver_t driver, uint32_t address, uint8_t word[4])
{
	if (register_address(address)) {
		return whd_bus_read_backplane_value(driver, address, 4, word);
	}
	whd_result_t rc = whd_bus_set_backplane_window(driver, address);

	return rc == WHD_SUCCESS
		       ? whd_bus_spi_read_register_value(driver, BACKPLANE_FUNCTION,
							 address & BACKPLANE_OFFSET_MASK, 4, word)
		       : rc;
}

static whd_result_t word_write(whd_driver_t driver, uint32_t address, uint32_t value)
{
	if (register_address(address)) {
		return whd_bus_write_backplane_value(driver, address, 4, value);
	}
	whd_result_t rc = whd_bus_set_backplane_window(driver, address);

	return rc == WHD_SUCCESS
		       ? whd_bus_spi_write_register_value(driver, BACKPLANE_FUNCTION,
							  address & BACKPLANE_OFFSET_MASK, 4, value)
		       : rc;
}

/* Use 32-bit access for control registers and byte access for RAM, matching
 * the firmware's backplane protocol. Stack buffers keep Bluetooth independent
 * of the Wi-Fi packet pool. Preserve bytes around unaligned firmware records.
 * The caller holds the shared mutex across the sequence.
 */
int cyw43_bus_read(whd_driver_t driver, uint32_t address, uint8_t *data, size_t len)
{
	uint8_t word[4];
	int rc = 0;

	if (whd_ensure_wlan_bus_is_up(driver) != WHD_SUCCESS) {
		rc = -EIO;
		goto out;
	}
	while (len) {
		size_t offset = address & 3;
		size_t count = MIN(len, 4 - offset);

		rc = word_read(driver, address & ~3U, word);
		if (rc != WHD_SUCCESS) {
			rc = -EIO;
			goto out;
		}
		memcpy(data, word + offset, count);
		address += count;
		data += count;
		len -= count;
	}
out:
	/* WHD power control accesses F1 registers assuming the chipcommon window. */
	if (whd_bus_set_backplane_window(driver, CHIPCOMMON_BASE_ADDRESS) != WHD_SUCCESS &&
	    rc == 0) {
		rc = -EIO;
	}
	return rc;
}

int cyw43_bus_write(whd_driver_t driver, uint32_t address, const uint8_t *data, size_t len)
{
	uint8_t word[4];
	int rc = 0;

	if (whd_ensure_wlan_bus_is_up(driver) != WHD_SUCCESS) {
		rc = -EIO;
		goto out;
	}
	while (len) {
		size_t offset = address & 3;
		size_t count = MIN(len, 4 - offset);

		if (count != 4) {
			rc = word_read(driver, address & ~3U, word);
			if (rc != WHD_SUCCESS) {
				rc = -EIO;
				goto out;
			}
		}
		memcpy(word + offset, data, count);
		rc = word_write(driver, address & ~3U, sys_get_le32(word));
		if (rc != WHD_SUCCESS) {
			rc = -EIO;
			goto out;
		}
		address += count;
		data += count;
		len -= count;
	}
out:
	/* WHD power control accesses F1 registers assuming the chipcommon window. */
	if (whd_bus_set_backplane_window(driver, CHIPCOMMON_BASE_ADDRESS) != WHD_SUCCESS &&
	    rc == 0) {
		rc = -EIO;
	}
	return rc;
}

/* Linker wrappers keep the pinned Zephyr/HAL sources untouched. Each wrapper
 * guards the entire operation, not just spi_transceive: window selection and
 * shared data/IRQ pin configuration are also mutable bus state.
 */
whd_result_t __real_whd_bus_spi_transfer(whd_driver_t driver, const uint8_t *tx, size_t tx_len,
					 uint8_t *rx, size_t rx_len, uint8_t fill);
whd_result_t __wrap_whd_bus_spi_transfer(whd_driver_t driver, const uint8_t *tx, size_t tx_len,
					 uint8_t *rx, size_t rx_len, uint8_t fill)
{
	whd_result_t rc;

	cyw43_bus_lock();
	rc = __real_whd_bus_spi_transfer(driver, tx, tx_len, rx, rx_len, fill);
	cyw43_bus_unlock();
	return rc;
}

whd_result_t __real_whd_bus_read_backplane_value(whd_driver_t driver, uint32_t address, uint8_t len,
						 uint8_t *value);
whd_result_t __wrap_whd_bus_read_backplane_value(whd_driver_t driver, uint32_t address, uint8_t len,
						 uint8_t *value)
{
	whd_result_t rc;

	cyw43_bus_lock();
	rc = __real_whd_bus_read_backplane_value(driver, address, len, value);
	cyw43_bus_unlock();
	return rc;
}

whd_result_t __real_whd_bus_write_backplane_value(whd_driver_t driver, uint32_t address,
						  uint8_t len, uint32_t value);
whd_result_t __wrap_whd_bus_write_backplane_value(whd_driver_t driver, uint32_t address,
						  uint8_t len, uint32_t value)
{
	whd_result_t rc;

	cyw43_bus_lock();
	rc = __real_whd_bus_write_backplane_value(driver, address, len, value);
	cyw43_bus_unlock();
	return rc;
}

whd_result_t __real_whd_bus_transfer_backplane_bytes(whd_driver_t driver,
						     whd_bus_transfer_direction_t direction,
						     uint32_t address, uint32_t size,
						     uint8_t *data);
whd_result_t __wrap_whd_bus_transfer_backplane_bytes(whd_driver_t driver,
						     whd_bus_transfer_direction_t direction,
						     uint32_t address, uint32_t size, uint8_t *data)
{
	whd_result_t rc;

	cyw43_bus_lock();
	rc = __real_whd_bus_transfer_backplane_bytes(driver, direction, address, size, data);
	cyw43_bus_unlock();
	return rc;
}

#define WRAP_BUS_POWER(name)                                                                       \
	whd_result_t __real_##name(whd_driver_t driver);                                           \
	whd_result_t __wrap_##name(whd_driver_t driver)                                            \
	{                                                                                          \
		whd_result_t rc;                                                                   \
		cyw43_bus_lock();                                                                  \
		rc = __real_##name(driver);                                                        \
		cyw43_bus_unlock();                                                                \
		return rc;                                                                         \
	}

WRAP_BUS_POWER(whd_bus_sleep)
WRAP_BUS_POWER(whd_bus_wakeup)

WRAP_BUS_POWER(whd_ensure_wlan_bus_is_up)
WRAP_BUS_POWER(whd_allow_wlan_bus_to_sleep)

whd_result_t __real_whd_bus_send_buffer(whd_driver_t driver, whd_buffer_t buffer);
whd_result_t __wrap_whd_bus_send_buffer(whd_driver_t driver, whd_buffer_t buffer)
{
	whd_result_t rc;

	cyw43_bus_lock();
	rc = __real_whd_bus_send_buffer(driver, buffer);
	cyw43_bus_unlock();
	return rc;
}

whd_result_t __real_whd_bus_read_frame(whd_driver_t driver, whd_buffer_t *buffer);
whd_result_t __wrap_whd_bus_read_frame(whd_driver_t driver, whd_buffer_t *buffer)
{
	whd_result_t rc;

	cyw43_bus_lock();
	rc = __real_whd_bus_read_frame(driver, buffer);
	cyw43_bus_unlock();
	return rc;
}

uint32_t __real_whd_bus_packet_available_to_read(whd_driver_t driver);
uint32_t __wrap_whd_bus_packet_available_to_read(whd_driver_t driver)
{
	uint32_t rc;

	cyw43_bus_lock();
	rc = __real_whd_bus_packet_available_to_read(driver);
	cyw43_bus_unlock();
	return rc;
}

whd_result_t __real_whd_bus_set_backplane_window(whd_driver_t driver, uint32_t address);
whd_result_t __wrap_whd_bus_set_backplane_window(whd_driver_t driver, uint32_t address)
{
	whd_result_t rc;

	cyw43_bus_lock();
	rc = __real_whd_bus_set_backplane_window(driver, address);
	cyw43_bus_unlock();
	return rc;
}
