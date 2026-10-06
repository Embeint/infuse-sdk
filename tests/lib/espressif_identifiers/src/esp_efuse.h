/* Copyright (c) 2026 Embeint Holdings Pty Ltd
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef TEST_ESP_EFUSE_H_
#define TEST_ESP_EFUSE_H_

#include <stddef.h>

#define EFUSE_BLK_USER_DATA 3
#define ESP_OK              0

int esp_efuse_read_block(int block, void *data, size_t offset, size_t bits);

#endif /* TEST_ESP_EFUSE_H_ */
