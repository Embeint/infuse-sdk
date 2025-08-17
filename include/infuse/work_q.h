/**
 * @file
 * @brief Infuse-IoT common work queue
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 * This work queue is intended for jobs that are not time critical to run,
 * or need to perform actions where the system workqueue is required to be
 * unblocked (Bluetooth).
 *
 * Currently this is used as the core work queue for the Task Runner subsystem.
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_WORK_Q_H_
#define INFUSE_SDK_INCLUDE_INFUSE_WORK_Q_H_

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Infuse-IoT Work Queue API
 * @defgroup infuse_work_q_apis Infuse-IoT Work Queue API
 * @{
 */

extern struct k_work_q infuse_iot_work_q;

/** @brief Submit a work item to the Infuse-IoT queue.
 *
 * @param work pointer to the work item.
 *
 * @return as with k_work_submit_to_queue().
 */
static inline int infuse_work_submit(struct k_work *work)
{
	return k_work_submit_to_queue(&infuse_iot_work_q, work);
}

/** @brief Submit an idle work item to the Infuse-IoT work queue after a delay.
 *
 * This is a thin wrapper around k_work_schedule_for_queue(), with all the API
 * characteristics of that function.
 *
 * @param dwork pointer to the delayable work item.
 *
 * @param delay the time to wait before submitting the work item.  If @c
 * K_NO_WAIT this is equivalent to k_work_submit_to_queue().
 *
 * @return as with k_work_schedule_for_queue().
 */
static inline int infuse_work_schedule(struct k_work_delayable *dwork, k_timeout_t delay)
{
	return k_work_schedule_for_queue(&infuse_iot_work_q, dwork, delay);
}

/** @brief Reschedule a work item to the Infuse-IoT work queue after a delay.
 *
 * This is a thin wrapper around k_work_reschedule_for_queue(), with all the
 * API characteristics of that function.
 *
 * @param dwork pointer to the delayable work item.
 *
 * @param delay the time to wait before submitting the work item.
 *
 * @return as with k_work_reschedule_for_queue().
 */
static inline int infuse_work_reschedule(struct k_work_delayable *dwork, k_timeout_t delay)
{
	return k_work_reschedule_for_queue(&infuse_iot_work_q, dwork, delay);
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_WORK_Q_H_ */
