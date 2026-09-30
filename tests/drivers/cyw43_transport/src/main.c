/* Copyright (c) 2026 Embeint Holdings Pty Ltd
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>
#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include "cyw43_transport.h"

#define TEST_RAM     0x100000U
#define BT_CONTROL   0x18000c7cU
#define HOST_CONTROL 0x18000d6cU
#define RAM_BASE     0x18000d68U
#define FW_BASE      0x19000000U

static uint8_t ram[0x2010];
static uint8_t fw_ram[32];
static struct cyw43_transport transport;
static int fail_read;
static int fail_write;
static int invalid_reads;
static int bus_depth;
static uint32_t ctrl;
static uint32_t reported_base;

void cyw43_bus_lock(void)
{
	bus_depth++;
}

void cyw43_bus_unlock(void)
{
	zassert_true(bus_depth > 0);
	bus_depth--;
}

int cyw43_bus_read(whd_driver_t driver, uint32_t address, uint8_t *data, size_t len)
{
	ARG_UNUSED(driver);
	zassert_equal(bus_depth, 1);
	if (fail_read) {
		return fail_read;
	}
	if (address == RAM_BASE) {
		sys_put_le32(reported_base, data);
	} else if (address == BT_CONTROL) {
		sys_put_le32(BIT(8) | BIT(24), data);
	} else if (address >= reported_base && address + len <= reported_base + sizeof(ram)) {
		memcpy(data, ram + address - reported_base, len);
		if (address == reported_base + 0x2000 && invalid_reads) {
			invalid_reads--;
			sys_put_le32(CYW43_RING_SIZE, data);
		}
	} else {
		zassert_unreachable("Unexpected read address 0x%x", address);
	}
	return 0;
}

int cyw43_bus_write(whd_driver_t driver, uint32_t address, const uint8_t *data, size_t len)
{
	ARG_UNUSED(driver);
	zassert_equal(bus_depth, 1);
	if (fail_write) {
		return fail_write;
	}
	if (address == HOST_CONTROL) {
		ctrl = sys_get_le32(data);
	} else if (address == FW_BASE + 0x640894) {
		zassert_equal(sys_get_le32(data), 3);
	} else if (address >= FW_BASE && address + len <= FW_BASE + sizeof(fw_ram)) {
		memcpy(fw_ram + address - FW_BASE, data, len);
	} else if (address >= reported_base && address + len <= reported_base + sizeof(ram)) {
		memcpy(ram + address - reported_base, data, len);
	} else {
		zassert_unreachable("Unexpected write address 0x%x", address);
	}
	return 0;
}

static void before(void *unused)
{
	ARG_UNUSED(unused);
	memset(ram, 0, sizeof(ram));
	memset(fw_ram, 0xff, sizeof(fw_ram));
	transport = (struct cyw43_transport){.ram_base = TEST_RAM};
	fail_read = 0;
	fail_write = 0;
	invalid_reads = 0;
	ctrl = 0;
	reported_base = TEST_RAM;
	zassert_equal(bus_depth, 0);
}

static void set_indices(uint32_t tx_in, uint32_t tx_out, uint32_t rx_in, uint32_t rx_out)
{
	sys_put_le32(tx_in, ram + 0x2000);
	sys_put_le32(tx_out, ram + 0x2004);
	sys_put_le32(rx_in, ram + 0x2008);
	sys_put_le32(rx_out, ram + 0x200c);
}

static void rx_frame(uint32_t offset, uint8_t type, const uint8_t *data, size_t len)
{
	uint8_t frame[64] = {0};
	size_t size = ROUND_UP(len + 4, 4);

	sys_put_le24(len, frame);
	frame[3] = type;
	memcpy(frame + 4, data, len);
	for (size_t i = 0; i < size; i++) {
		ram[4096 + ((offset + i) & 4095)] = frame[i];
	}
	set_indices(0, 0, (offset + size) & 4095, offset);
}

ZTEST(cyw43_transport, test_send_padding)
{
	uint8_t payload[] = {1, 2, 3};

	memset(ram, 0xa5, 16);
	zassert_ok(cyw43_transport_send(&transport, 1, payload, sizeof(payload)));
	zassert_equal(sys_get_le24(ram), sizeof(payload));
	zassert_equal(ram[3], 1);
	zassert_mem_equal(ram + 4, payload, sizeof(payload));
	zassert_equal(ram[7], 0);
	zassert_equal(sys_get_le32(ram + 0x2000), 8);
	zassert_equal(ctrl, BIT(1));
}

ZTEST(cyw43_transport, test_send_wrap)
{
	uint8_t payload[] = {1, 2, 3, 4, 5, 6};

	set_indices(4088, 128, 0, 0);
	zassert_ok(cyw43_transport_send(&transport, 2, payload, sizeof(payload)));
	zassert_mem_equal(ram + 4092, payload, 4);
	zassert_mem_equal(ram, payload + 4, 2);
	zassert_equal(sys_get_le32(ram + 0x2000), 4);
}

ZTEST(cyw43_transport, test_full_tx_ring)
{
	const uint8_t payload[] = {1};

	set_indices(0, 4, 0, 0);
	zassert_equal(cyw43_transport_send(&transport, 1, payload, 1), -EAGAIN);
	zassert_equal(sys_get_le32(ram + 0x2000), 0);
	zassert_equal(ctrl, 0);
}

ZTEST(cyw43_transport, test_send_error_does_not_commit)
{
	const uint8_t payload[] = {1};

	fail_write = -EIO;
	zassert_equal(cyw43_transport_send(&transport, 1, payload, 1), -EIO);
	zassert_equal(sys_get_le32(ram + 0x2000), 0);
	zassert_equal(bus_depth, 0);
}

ZTEST(cyw43_transport, test_send_too_large)
{
	zassert_equal(cyw43_transport_send(&transport, 1, NULL, CYW43_PACKET_MAX + 1), -EMSGSIZE);
}

ZTEST(cyw43_transport, test_receive_empty)
{
	uint8_t data[8], type;

	zassert_equal(cyw43_transport_recv(&transport, &type, data, sizeof(data)), 0);
	zassert_equal(ctrl, 0);
}

ZTEST(cyw43_transport, test_receive_wrap)
{
	const uint8_t payload[] = {1, 2, 3, 4, 5, 6};
	uint8_t data[8], type;

	rx_frame(4088, 4, payload, sizeof(payload));
	zassert_equal(cyw43_transport_recv(&transport, &type, data, sizeof(data)), sizeof(payload));
	zassert_equal(type, 4);
	zassert_mem_equal(data, payload, sizeof(payload));
	zassert_equal(sys_get_le32(ram + 0x200c), 4);
}

ZTEST(cyw43_transport, test_receive_partial)
{
	uint8_t data[8], type;

	sys_put_le24(8, ram + 4096);
	set_indices(0, 0, 4, 0);
	zassert_equal(cyw43_transport_recv(&transport, &type, data, sizeof(data)), -EAGAIN);
	zassert_equal(sys_get_le32(ram + 0x200c), 0);
}

ZTEST(cyw43_transport, test_receive_oversize_then_valid)
{
	const uint8_t payload[] = {1, 2, 3, 4, 5, 6};
	uint8_t data[4], type;

	rx_frame(0, 4, payload, sizeof(payload));
	zassert_equal(cyw43_transport_recv(&transport, &type, data, sizeof(data)), -EMSGSIZE);
	zassert_equal(sys_get_le32(ram + 0x200c), 12);
	rx_frame(12, 4, payload, 2);
	zassert_equal(cyw43_transport_recv(&transport, &type, data, sizeof(data)), 2);
	zassert_mem_equal(data, payload, 2);
}

ZTEST(cyw43_transport, test_receive_invalid_header)
{
	uint8_t data[8], type;

	sys_put_le24(0xffffff, ram + 4096);
	set_indices(0, 0, 8, 0);
	zassert_equal(cyw43_transport_recv(&transport, &type, data, sizeof(data)), -EBADMSG);
	zassert_equal(sys_get_le32(ram + 0x200c), 8);
}

ZTEST(cyw43_transport, test_transient_invalid_index)
{
	uint8_t data[8], type;

	invalid_reads = 1;
	zassert_equal(cyw43_transport_recv(&transport, &type, data, sizeof(data)), 0);
	zassert_equal(invalid_reads, 0);
}

ZTEST(cyw43_transport, test_persistent_invalid_index)
{
	uint8_t data[8], type;

	invalid_reads = 4;
	zassert_equal(cyw43_transport_recv(&transport, &type, data, sizeof(data)), -EIO);
	zassert_equal(invalid_reads, 1);
}

ZTEST(cyw43_transport, test_read_failure)
{
	uint8_t data[8], type;

	fail_read = -EIO;
	zassert_equal(cyw43_transport_recv(&transport, &type, data, sizeof(data)), -EIO);
	zassert_equal(bus_depth, 0);
}

/* Version, record count, extended address, unaligned data, EOF. */
static const uint8_t firmware[] = {
	2, 'v', 0, 3, 2, 0, 0, 4, 0, 0, 3, 0, 1, 0, 0x12, 0x34, 0x56, 0, 0, 0, 1,
};

ZTEST(cyw43_transport, test_firmware_load)
{
	zassert_ok(cyw43_transport_open(&transport, firmware, sizeof(firmware)));
	zassert_mem_equal(fw_ram + 1, firmware + 14, 3);
	zassert_equal(fw_ram[0], 0xff);
	zassert_equal(fw_ram[4], 0xff);
	zassert_equal(ctrl, BIT(17) | BIT(24) | BIT(1));
}

ZTEST(cyw43_transport, test_firmware_truncated)
{
	zassert_equal(cyw43_transport_open(&transport, firmware, sizeof(firmware) - 1), -EINVAL);
	zassert_equal(bus_depth, 0);
}

ZTEST(cyw43_transport, test_firmware_record_bounds)
{
	uint8_t bad[sizeof(firmware)];

	memcpy(bad, firmware, sizeof(bad));
	bad[10] = 255;
	zassert_equal(cyw43_transport_open(&transport, bad, sizeof(bad)), -EINVAL);
	zassert_equal(fw_ram[1], 0xff);
}

ZTEST(cyw43_transport, test_firmware_error)
{
	fail_write = -EIO;
	zassert_equal(cyw43_transport_open(&transport, firmware, sizeof(firmware)), -EIO);
	zassert_equal(bus_depth, 0);
}

ZTEST(cyw43_transport, test_zero_ram_base)
{
	reported_base = 0;
	memset(ram, 0xa5, sizeof(ram));
	zassert_equal(cyw43_transport_open(&transport, firmware, sizeof(firmware)), -EIO);
	for (size_t i = 0; i < sizeof(ram); i++) {
		zassert_equal(ram[i], 0xa5, "Wi-Fi RAM overwritten at %zu", i);
	}
	zassert_equal(ctrl, 0);
	zassert_equal(bus_depth, 0);
}

ZTEST(cyw43_transport, test_invalid_ram_base)
{
	reported_base = 3;
	zassert_equal(cyw43_transport_open(&transport, firmware, sizeof(firmware)), -EIO);
}

ZTEST_SUITE(cyw43_transport, NULL, NULL, before, NULL, NULL);
