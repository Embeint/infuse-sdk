/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Anton Schieber <anton@lodienabled.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <infuse/math/mean.h>

void mean_update(struct mean_state *state, int32_t x)
{
	int32_t m1 = state->m;
	uint32_t n1 = state->n;
	uint32_t n = n1 + 1;

	state->n = n;
	if (n == 1) {
		state->m = x;
		return;
	}

	state->m = m1 + (x - m1) / n;
}
