/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <infuse/work_q.h>

struct k_work_q infuse_iot_work_q;

static K_THREAD_STACK_DEFINE(infuse_workq_stack_area, CONFIG_INFUSE_WORKQ_STACK_SIZE);

static int infuse_iot_work_queue_init(void)
{
	/* Boot the task runner workqueue */
	k_work_queue_init(&infuse_iot_work_q);
	k_work_queue_start(&infuse_iot_work_q, infuse_workq_stack_area,
			   K_THREAD_STACK_SIZEOF(infuse_workq_stack_area),
			   CONFIG_SYSTEM_WORKQUEUE_PRIORITY, NULL);
	k_thread_name_set(k_work_queue_thread_get(&infuse_iot_work_q), "infuse_workq");
	return 0;
}

SYS_INIT(infuse_iot_work_queue_init, POST_KERNEL, 0);
