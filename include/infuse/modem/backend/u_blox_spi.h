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

#include "common.h"

#ifndef INFUSE_MODEM_BACKEND_U_BLOX_SPI_
#define INFUSE_MODEM_BACKEND_U_BLOX_SPI_

#ifdef __cplusplus
extern "C" {
#endif

struct modem_backend_ublox_spi {
	/* Common backend */
	struct modem_backend_common common;
	/* Pointer to SPI configuration */
	const struct spi_dt_spec *spi;
	/* Temporary RX buffer */
	uint8_t spi_rx[70];
};

struct modem_backend_ublox_spi_config {
	/* Bus to use */
	const struct spi_dt_spec *spi;
	/* GPIO that will transition to active when data is ready after @ref
	 * modem_backend_ublox_spi_use_data_ready_gpio
	 */
	const struct gpio_dt_spec *data_ready;
	/* Period to poll for data before @ref modem_backend_ublox_spi_use_data_ready_gpio */
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
modem_backend_ublox_spi_init(struct modem_backend_ublox_spi *backend,
			     const struct modem_backend_ublox_spi_config *config);

/**
 * @brief Transition from polling to interrupt driven mode
 *
 * @param backend Modem backend data structure
 */
void modem_backend_ublox_spi_use_data_ready_gpio(struct modem_backend_ublox_spi *backend);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_MODEM_BACKEND_U_BLOX_SPI_ */
