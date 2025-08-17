/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <infuse/drivers/imu.h>

int imu_linear_downsample_scaled(struct imu_linear_downsample_scaled_state *state,
				 const struct imu_sample *input, uint16_t num_input)
{
	const struct imu_sample *prev = &state->last_sample;
	int diff_x, diff_y, diff_z;
	int out_x, out_y, out_z;
	bool write_first = state->subsample_idx == 0;

	for (int i = 0; i < num_input; i++) {
		if ((state->subsample_idx >= state->freq_div) || write_first) {
			int suboffset = state->subsample_idx - state->freq_div;
			int ratio = state->freq_mult - suboffset;

			if (write_first) {
				out_x = input->x;
				out_y = input->y;
				out_z = input->z;
			} else {
				/* Compute diff */
				diff_x = input->x - prev->x;
				diff_y = input->y - prev->y;
				diff_z = input->z - prev->z;
				/* Compute resampled values */
				out_x = prev->x + (ratio * diff_x) / state->freq_mult;
				out_y = prev->y + (ratio * diff_y) / state->freq_mult;
				out_z = prev->z + (ratio * diff_z) / state->freq_mult;
			}

			/* Write output in scaled form */
			state->output_x[state->output_offset] = (float)out_x / state->scale;
			state->output_y[state->output_offset] = (float)out_y / state->scale;
			state->output_z[state->output_offset] = (float)out_z / state->scale;
			state->output_offset += 1;
			if (!write_first) {
				state->subsample_idx -= state->freq_div;
			}
			write_first = false;

			if (state->output_offset == state->output_size) {
				/* Output buffer filled */
				state->last_sample = *input;
				state->subsample_idx += state->freq_mult;
				return i + 1;
			}
		}

		state->subsample_idx += state->freq_mult;
		prev = input;
		input += 1;
	}
	/* Save last sample for next buffer */
	input -= 1;
	state->last_sample = *input;
	/* All samples consumed */
	return num_input;
}
