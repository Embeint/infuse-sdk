/*
 * Copyright (c) 2026 Embeint Holdings Pty Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef INFUSE_SDK_TESTS_SUBSYS_ALGORITHM_RUNNER_LLEXT_TEST_ALGORITHM_H_
#define INFUSE_SDK_TESTS_SUBSYS_ALGORITHM_RUNNER_LLEXT_TEST_ALGORITHM_H_

#include <stddef.h>
#include <stdint.h>

#define TEST_ALGORITHM_FILE_ID                 0x12349876
#define TEST_ALGORITHM_2_FILE_ID               0x87654321
#define TEST_ALGORITHM_INVALID_VERSION_FILE_ID 0x12344321

int test_algorithm_install(void);
int test_algorithm_2_install(void);
int test_algorithm_invalid_version_install(void);
const uint8_t *test_algorithm_data(void);
size_t test_algorithm_size(void);

#endif /* INFUSE_SDK_TESTS_SUBSYS_ALGORITHM_RUNNER_LLEXT_TEST_ALGORITHM_H_ */
