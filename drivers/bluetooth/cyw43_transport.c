/*
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
 * Copyright (c) 2026 Embeint Holdings Pty Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#include "cyw43_transport.h"

#define BT_MEMORY_BASE   0x19000000U
#define BT_POWER_CONTROL (BT_MEMORY_BASE + 0x640894U)
#define BT_CONTROL       0x18000c7cU
#define HOST_CONTROL     0x18000d6cU
#define RAM_BASE         0x18000d68U
#define HOST_WAKE        BIT(17)
#define HOST_READY       BIT(24)
#define HOST_DATA_VALID  BIT(1)
#define BT_READY         BIT(24)
#define BT_AWAKE         BIT(8)
#define H2B_IN           0x2000U
#define H2B_OUT          0x2004U
#define B2H_IN           0x2008U
#define B2H_OUT          0x200cU

static int read_word(struct cyw43_transport *t, uint32_t address, uint32_t *value)
{
	uint8_t data[4];
	int rc = cyw43_bus_read(t->driver, address, data, sizeof(data));

	if (rc == 0) {
		*value = sys_get_le32(data);
	}
	return rc;
}

static int write_word(struct cyw43_transport *t, uint32_t address, uint32_t value)
{
	uint8_t data[4];

	sys_put_le32(value, data);
	return cyw43_bus_write(t->driver, address, data, sizeof(data));
}

static int host_update(struct cyw43_transport *t, uint32_t set, uint32_t toggle)
{
	uint32_t value = (t->host_control | set) ^ toggle;
	int rc = write_word(t, HOST_CONTROL, value);

	if (rc == 0) {
		t->host_control = value;
	}
	return rc;
}

static int wait_control(struct cyw43_transport *t, uint32_t mask)
{
	int64_t deadline = k_uptime_get() + 300;
	uint32_t value;
	int rc;

	do {
		rc = read_word(t, BT_CONTROL, &value);
		if (rc || ((value & mask) == mask)) {
			return rc;
		}
		k_sleep(K_MSEC(1));
	} while (k_uptime_get() < deadline);
	return -ETIMEDOUT;
}

/* Decode the compact Intel HEX records in the pinned firmware. Validate every
 * boundary before touching RAM, including address records and the terminator.
 */
static int firmware_load(struct cyw43_transport *t, const uint8_t *fw, size_t len)
{
	uint32_t base = 0;
	size_t pos;
	int rc;

	if (len < 3 || fw[0] == 0 || (size_t)fw[0] + 2 > len || fw[fw[0]] != 0) {
		return -EINVAL;
	}
	pos = fw[0] + 2;
	rc = write_word(t, BT_POWER_CONTROL, 3);
	if (rc) {
		return rc;
	}
	while (len - pos >= 4) {
		uint8_t count = fw[pos];
		uint16_t address = sys_get_be16(fw + pos + 1);
		uint8_t type = fw[pos + 3];

		pos += 4;
		if (count > len - pos) {
			return -EINVAL;
		}
		switch (type) {
		case 0:
			if (count == 0 || base > UINT32_MAX - BT_MEMORY_BASE - address ||
			    count > UINT32_MAX - (BT_MEMORY_BASE + base + address)) {
				return -EINVAL;
			}
			rc = cyw43_bus_write(t->driver, BT_MEMORY_BASE + base + address, fw + pos,
					     count);
			if (rc) {
				return rc;
			}
			break;
		case 1:
			return (count == 0 && pos == len) ? 0 : -EINVAL;
		case 2:
		case 4:
			if (count != 2) {
				return -EINVAL;
			}
			base = (uint32_t)sys_get_be16(fw + pos) << (type == 2 ? 4 : 16);
			break;
		case 5:
			if (count != 4) {
				return -EINVAL;
			}
			base = sys_get_be32(fw + pos);
			break;
		default:
			return -EINVAL;
		}
		pos += count;
	}
	return -EINVAL;
}

int cyw43_transport_open(struct cyw43_transport *t, const uint8_t *firmware, size_t len)
{
	int rc;

	cyw43_bus_lock();
	t->host_control = 0;
	rc = firmware_load(t, firmware, len);
	if (rc) {
		goto out;
	}
	k_sleep(K_MSEC(150));
	rc = wait_control(t, BT_READY);
	if (rc) {
		goto out;
	}
	rc = read_word(t, RAM_BASE, &t->ram_base);
	if (rc) {
		goto out;
	}
	/* A missing btsdio firmware feature leaves this register zero. Never
	 * let Bluetooth overwrite the Wi-Fi firmware at the start of RAM.
	 */
	if (t->ram_base == 0 || (t->ram_base & 3) || t->ram_base > UINT32_MAX - B2H_OUT) {
		rc = -EIO;
		goto out;
	}
	for (uint32_t offset = H2B_IN; offset <= B2H_OUT; offset += 4) {
		rc = write_word(t, t->ram_base + offset, 0);
		if (rc) {
			goto out;
		}
	}
	rc = host_update(t, HOST_WAKE, 0);
	if (rc == 0) {
		rc = wait_control(t, BT_AWAKE);
	}
	if (rc == 0) {
		rc = host_update(t, HOST_READY, HOST_DATA_VALID);
	}
out:
	cyw43_bus_unlock();
	return rc;
}

static int indices_read(struct cyw43_transport *t, uint32_t indices[4])
{
	uint8_t data[16];
	int rc;

	/* The controller can expose transient invalid indices. Retry with a bound;
	 * never use an invalid value as a memory offset or assert on radio input.
	 */
	for (int attempt = 0; attempt < 3; attempt++) {
		rc = cyw43_bus_read(t->driver, t->ram_base + H2B_IN, data, sizeof(data));
		if (rc) {
			return rc;
		}
		bool valid = true;

		for (int i = 0; i < 4; i++) {
			indices[i] = sys_get_le32(data + 4 * i);
			valid &= (indices[i] < CYW43_RING_SIZE) && ((indices[i] & 3) == 0);
		}
		if (valid) {
			return 0;
		}
		k_sleep(K_MSEC(1));
	}
	return -EIO;
}

static int ring_read(struct cyw43_transport *t, uint32_t offset, uint8_t *data, size_t len)
{
	size_t first = MIN(len, CYW43_RING_SIZE - offset);
	int rc = cyw43_bus_read(t->driver, t->ram_base + CYW43_RING_SIZE + offset, data, first);

	if (rc == 0 && len > first) {
		rc = cyw43_bus_read(t->driver, t->ram_base + CYW43_RING_SIZE, data + first,
				    len - first);
	}
	return rc;
}

static int ring_write(struct cyw43_transport *t, uint32_t offset, const uint8_t *data, size_t len)
{
	size_t first = MIN(len, CYW43_RING_SIZE - offset);
	int rc = cyw43_bus_write(t->driver, t->ram_base + offset, data, first);

	if (rc == 0 && len > first) {
		rc = cyw43_bus_write(t->driver, t->ram_base, data + first, len - first);
	}
	return rc;
}

int cyw43_transport_send(struct cyw43_transport *t, uint8_t type, const uint8_t *data, size_t len)
{
	uint8_t header[4];
	const uint8_t padding[3] = {0};
	uint32_t indices[4];
	size_t size = ROUND_UP(len + 4, 4);
	int rc;

	if (len > CYW43_PACKET_MAX) {
		return -EMSGSIZE;
	}
	sys_put_le24(len, header);
	header[3] = type;
	cyw43_bus_lock();
	rc = indices_read(t, indices);
	if (rc) {
		goto out;
	}
	if (size > ((indices[1] - indices[0] - 4) & (CYW43_RING_SIZE - 1))) {
		rc = -EAGAIN;
		goto out;
	}
	rc = ring_write(t, indices[0], header, sizeof(header));
	if (rc == 0) {
		rc = ring_write(t, (indices[0] + 4) & (CYW43_RING_SIZE - 1), data, len);
	}
	if (rc == 0 && size != len + 4) {
		rc = ring_write(t, (indices[0] + 4 + len) & (CYW43_RING_SIZE - 1), padding,
				size - len - 4);
	}
	if (rc == 0) {
		rc = write_word(t, t->ram_base + H2B_IN,
				(indices[0] + size) & (CYW43_RING_SIZE - 1));
	}
	if (rc == 0) {
		rc = host_update(t, 0, HOST_DATA_VALID);
	}
out:
	cyw43_bus_unlock();
	return rc;
}

int cyw43_transport_recv(struct cyw43_transport *t, uint8_t *type, uint8_t *data, size_t capacity)
{
	uint8_t header[4];
	uint32_t indices[4];
	size_t available, len, size;
	int rc;

	cyw43_bus_lock();
	rc = indices_read(t, indices);
	if (rc) {
		goto out;
	}
	available = (indices[2] - indices[3]) & (CYW43_RING_SIZE - 1);
	if (available == 0) {
		goto out;
	}
	rc = ring_read(t, indices[3], header, sizeof(header));
	if (rc) {
		goto out;
	}
	len = sys_get_le24(header);
	size = ROUND_UP(len + 4, 4);
	if (size >= CYW43_RING_SIZE || len == 0) {
		/* Lose synchronization only on an invalid frame: discard this snapshot
		 * so a corrupt header cannot block every subsequent HCI command.
		 */
		rc = write_word(t, t->ram_base + B2H_OUT, indices[2]);
		if (rc == 0) {
			rc = host_update(t, 0, HOST_DATA_VALID);
		}
		rc = rc ? rc : -EBADMSG;
		goto out;
	}
	if (size > available) {
		/* Keep the header until the controller has committed the whole frame. */
		rc = -EAGAIN;
		goto out;
	}
	if (len <= capacity) {
		rc = ring_read(t, (indices[3] + 4) & (CYW43_RING_SIZE - 1), data, len);
		if (rc) {
			goto out;
		}
	}
	rc = write_word(t, t->ram_base + B2H_OUT, (indices[3] + size) & (CYW43_RING_SIZE - 1));
	if (rc == 0) {
		rc = host_update(t, 0, HOST_DATA_VALID);
	}
	if (rc == 0) {
		*type = header[3];
		rc = len <= capacity ? (int)len : -EMSGSIZE;
	}
out:
	cyw43_bus_unlock();
	return rc;
}
