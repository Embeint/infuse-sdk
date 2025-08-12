/**
 * Copyright (c) 2024 Croxel, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <infuse/reboot.h>
#include "bstests.h"
#include "common.h"

static K_SEM_DEFINE(reboot_request, 0, 1);

extern enum bst_result_t bst_result;

struct k_sem *test_get_reboot_sem(void)
{
	return &reboot_request;
}

int infuse_reboot_state_query(struct infuse_reboot_state *state)
{
	return -ENOENT;
}

void infuse_reboot(enum infuse_reboot_reason reason, uint32_t info1, uint32_t info2)
{
	k_sem_give(&reboot_request);
}

void infuse_reboot_delayed(enum infuse_reboot_reason reason, uint32_t info1, uint32_t info2,
			   k_timeout_t delay)
{
	k_sem_give(&reboot_request);
}

void test_tick(bs_time_t HW_device_time)
{
	if (bst_result != Passed) {
		FAIL("test failed (not passed after %i seconds)\n", WAIT_SECONDS);
	}
}

void test_init(void)
{
	bst_ticker_set_next_tick_absolute(WAIT_TIME);
	bst_result = In_progress;
}
