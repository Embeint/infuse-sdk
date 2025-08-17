/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/logging/log.h>

#include "gnss_common.h"

#define KM (1000 * 1000)

LOG_MODULE_DECLARE(task_gnss, CONFIG_TASK_GNSS_LOG_LEVEL);

bool gnss_run_to_fix_timeout(const struct task_gnss_args *args,
			     struct gnss_fix_timeout_state *state, uint32_t h_acc, uint32_t runtime)
{
	const struct task_gnss_plateau_args *p_args = &args->run_to_fix.fix_plateau;
	int32_t required_next_accuracy;
	bool plateau_valid;

	if (args->run_to_fix.any_fix_timeout) {
		/* Terminate if fix hasn't reached 10km accuracy by the initial timeout */
		if ((h_acc > (10 * KM)) && (runtime >= args->run_to_fix.any_fix_timeout)) {
			LOG_INF("Terminating due to %s", "any fix timeout");
			return true;
		}
	}

	/* Plateau check should be performed if:
	 *    1. Timeout is enabled
	 *    2. Accuracy reaches some hardcoded minimum threshold
	 *    3. Tighter minimum accuracy not requested, or reached
	 */
	plateau_valid = p_args->timeout && (h_acc < (10 * KM)) &&
			((p_args->min_accuracy_m == 0) ||
			 (h_acc <= (1000 * (uint32_t)p_args->min_accuracy_m)));
	if (!plateau_valid) {
		return false;
	}

	/* Accuracy must improve by at least `min_accuracy_improvement_m` */
	required_next_accuracy =
		state->plateau_accuracy - (1000 * p_args->min_accuracy_improvement_m);

	if (h_acc <= required_next_accuracy) {
		/* Accuracy improved, reset timeout and update current accuracy */
		state->plateau_accuracy = h_acc;
		state->plateau_timeout = p_args->timeout;
	} else {
		/* Accuracy not yet improved, tick timeout */
		state->plateau_timeout--;
	}
	if (state->plateau_timeout == 0) {
		/* Timed out */
		LOG_INF("Terminating due to %s", "accuracy plateau");
		return true;
	}
	return false;
}
