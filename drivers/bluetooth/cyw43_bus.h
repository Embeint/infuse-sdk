/* Copyright (c) 2026 Embeint Holdings Pty Ltd
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */
#ifndef INFUSE_CYW43_BUS_H_
#define INFUSE_CYW43_BUS_H_

#include <whd.h>

void cyw43_bus_lock(void);
void cyw43_bus_unlock(void);
int cyw43_bus_read(whd_driver_t driver, uint32_t address, uint8_t *data, size_t len);
int cyw43_bus_write(whd_driver_t driver, uint32_t address, const uint8_t *data, size_t len);

#endif
