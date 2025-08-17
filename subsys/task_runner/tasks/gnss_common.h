/**
 * @file
 * @brief Common GNSS logic
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_SUBSYS_TASK_RUNNER_TASKS_GNSS_H_
#define INFUSE_SDK_SUBSYS_TASK_RUNNER_TASKS_GNSS_H_

#include <stdint.h>

#include <infuse/task_runner/tasks/gnss_args.h>

/** State for tracking fix timeouts */
struct gnss_fix_timeout_state {
	uint32_t plateau_accuracy;
	uint8_t plateau_timeout;
};

static inline void gnss_timeout_reset(struct gnss_fix_timeout_state *state)
{
	state->plateau_accuracy = UINT32_MAX;
	state->plateau_timeout = UINT8_MAX;
}

/**
 * @brief Check whether the fix should be terminated due to timeouts or accuracy plateau
 *
 * @param args Task arguments pointer
 * @param state State for tracking timeout
 * @param h_acc Current horizontal accuracy in mm
 * @param runtime Duration the fix has been running for
 *
 * @return true Fix should be terminated
 * @return false Fix should continue
 */
bool gnss_run_to_fix_timeout(const struct task_gnss_args *args,
			     struct gnss_fix_timeout_state *state, uint32_t h_acc,
			     uint32_t runtime);

#endif /* INFUSE_SDK_SUBSYS_TASK_RUNNER_TASKS_GNSS_H_ */
