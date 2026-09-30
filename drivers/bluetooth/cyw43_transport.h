/* Copyright (c) 2026 Embeint Holdings Pty Ltd
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */
#ifndef INFUSE_CYW43_TRANSPORT_H_
#define INFUSE_CYW43_TRANSPORT_H_

#include "cyw43_bus.h"

#define CYW43_RING_SIZE  4096
#define CYW43_PACKET_MAX 1024

struct cyw43_transport {
	whd_driver_t driver;
	uint32_t ram_base;
	uint32_t host_control;
};

int cyw43_transport_open(struct cyw43_transport *transport, const uint8_t *firmware, size_t len);
int cyw43_transport_send(struct cyw43_transport *transport, uint8_t type, const uint8_t *data,
			 size_t len);
int cyw43_transport_recv(struct cyw43_transport *transport, uint8_t *type, uint8_t *data,
			 size_t capacity);

#endif
