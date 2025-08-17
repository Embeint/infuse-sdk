/*
 * Copyright (c) 2024 Embeint Holdings Pty Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/ring_buffer.h>

#include <zephyr/modem/pipe.h>

#ifndef INFUSE_MODEM_BACKEND_U_BLOX_I2C_
#define INFUSE_MODEM_BACKEND_U_BLOX_I2C_

#ifdef __cplusplus
extern "C" {
#endif

struct modem_backend_ublox_i2c {
	/* Pointer to I2C configuration */
	const struct i2c_dt_spec *i2c;
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

struct modem_backend_ublox_i2c_config {
	/* Bus to use */
	const struct i2c_dt_spec *i2c;
	/* GPIO that will transition to active when data is ready after @ref
	 * modem_backend_ublox_i2c_use_data_ready_gpio
	 */
	const struct gpio_dt_spec *data_ready;
	/* Period to poll for data before @ref modem_backend_ublox_i2c_use_data_ready_gpio */
	k_timeout_t poll_period;
};

/**
 * @brief Initialize modem backend
 *
 * @param backend Modem backend data structure
 * @param config Modem backend configuration
 *
 * @return Pointer to the modem pipe
 */
struct modem_pipe *
modem_backend_ublox_i2c_init(struct modem_backend_ublox_i2c *backend,
			     const struct modem_backend_ublox_i2c_config *config);

/**
 * @brief Transition from polling to interrupt driven mode
 *
 * @param backend Modem backend data structure
 */
void modem_backend_ublox_i2c_use_data_ready_gpio(struct modem_backend_ublox_i2c *backend);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_MODEM_BACKEND_U_BLOX_I2C_ */
