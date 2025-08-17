/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <infuse/math/statistics.h>

void statistics_update(struct statistics_state *state, int32_t x)
{
	int32_t m1 = state->m;
	int32_t p1 = state->p;
	int64_t w1 = state->w;
	int64_t v1 = state->v;
	uint32_t n1 = state->n;
	uint32_t n = n1 + 1;

	state->n = n;
	if (n == 1) {
		state->m = x;
		return;
	}

	int64_t normalised = (int64_t)x - m1;

	/* Equation 4 */
	int64_t p_ = p1 + normalised;
	/* Equation 5 */
	int64_t delta_m = p_ / n;
	/* Equation 6 */
	state->m = m1 + delta_m;
	/* Equation 7 */
	state->p = p_ - (n * delta_m);

	/* Equation 11 */
	int64_t w_ = w1 + (normalised * normalised) - v1;
	/* Equation 12 */
	int64_t w__ = w_ - (2 * delta_m * p_) + (n * delta_m * delta_m);
	/* Equation 13 */
	int64_t delta_v = w__ / n1;

	/* Equation 14 */
	state->v = v1 + delta_v;
	/* Equation 15 */
	state->w = w__ - (n1 * delta_v);
}
