/*
 * Copyright (c) 2025 Embeint Holdings Pty Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/modem/pipe.h>
#include <zephyr/sys/ring_buffer.h>

struct modem_backend_common {
	/* Pointer to data ready pin configuration */
	const struct gpio_dt_spec *data_ready;
	/* Communication pipe */
	struct modem_pipe pipe;
	/* Ring buffer for holding pipe data stream */
	struct ring_buf pipe_ring_buf;
	/* Worker that reads the FIFO */
	struct k_work_delayable fifo_read;
	/* Signal to notify when FIFO read completes */
	struct k_poll_signal read_result;
	/* Callback for data ready GPIO */
	struct gpio_callback data_ready_cb;
	/* Bus (RTIO SQE) contention semaphore */
	struct k_sem bus_sem;
	/* Duration between polls */
	k_timeout_t poll_period;
	/* Number of bytes pending on FIFO */
	uint16_t bytes_pending;
	/* Internal state flags */
	uint8_t flags;
	/* Backing memory for pipe data stream */
	uint8_t pipe_memory[CONFIG_GNSS_U_BLOX_PIPE_SIZE];
};
