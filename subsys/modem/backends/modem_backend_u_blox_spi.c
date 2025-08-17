/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/logging/log.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/__assert.h>

#include <infuse/modem/backend/u_blox_spi.h>

LOG_MODULE_REGISTER(modem_ublox_spi, LOG_LEVEL_INF);

enum {
	MODE_BOOTING = BIT(0),
	MODE_POLLING = BIT(1),
	MODE_CLOSED = BIT(2),
};

void fifo_read_cb(const struct device *dev, int result, void *data)
{
	struct modem_backend_ublox_spi *backend = data;
	int FF_counter = 0;
	uint32_t written;

	/* Put received data in buffer */
	written = ring_buf_put(&backend->pipe_ring_buf, backend->spi_rx, sizeof(backend->spi_rx));
	if (written != sizeof(backend->spi_rx) && !(backend->flags & MODE_BOOTING)) {
		LOG_WRN("Dropped %u bytes", sizeof(backend->spi_rx) - written);
	}
	/* Notify consumers that data exists to read */
	modem_pipe_notify_receive_ready(&backend->pipe);

	/* If in polling mode, or query failed */
	if ((backend->flags & MODE_POLLING) || (result != 0)) {
		/* Reschedule another data poll */
		k_work_reschedule(&backend->fifo_read, backend->poll_period);
	}

	/* Check if port is idle (backwards so we can exit early) */
	for (int i = sizeof(backend->spi_rx) - 1; (i >= 0) && (backend->spi_rx[i] == 0xFF); i--) {
		FF_counter++;
	}

	if (FF_counter == sizeof(backend->spi_rx)) {
		LOG_DBG("RX: %d 0xFF bytes", sizeof(backend->spi_rx));
	} else {
		LOG_HEXDUMP_DBG(backend->spi_rx, sizeof(backend->spi_rx), "RX");
	}

#ifdef CONFIG_MODEM_BACKEND_U_BLOX_SPI_PM_MODE_BURST
	/* Release bus */
	pm_device_runtime_put_async(backend->spi->bus, K_MSEC(10));
#endif

	/* Release bus semaphore */
	k_sem_give(&backend->bus_sem);

	/* Notify transmit idle */
	modem_pipe_notify_transmit_idle(&backend->pipe);

	/* Still data pending, queue again */
	if (FF_counter < 50) {
		k_work_reschedule(&backend->fifo_read, K_NO_WAIT);
	} else if (backend->flags & MODE_BOOTING) {
		/* Initial junk has been purged */
		LOG_DBG("Modem pipe opened");
		backend->flags ^= MODE_BOOTING;
		modem_pipe_notify_opened(&backend->pipe);
	}
}

static void fifo_read_trigger(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct modem_backend_ublox_spi *backend =
		CONTAINER_OF(dwork, struct modem_backend_ublox_spi, fifo_read);
	const struct spi_buf rx = {
		.buf = backend->spi_rx,
		.len = sizeof(backend->spi_rx),
	};
	const struct spi_buf_set rxbs = {
		.buffers = &rx,
		.count = 1,
	};
	int rc;

	if (backend->flags & MODE_CLOSED) {
		return;
	}

	if (k_sem_take(&backend->bus_sem, K_NO_WAIT) < 0) {
		k_work_reschedule(&backend->fifo_read, K_MSEC(10));
		return;
	}

#ifdef CONFIG_MODEM_BACKEND_U_BLOX_SPI_PM_MODE_BURST
	/* Power up SPI */
	pm_device_runtime_get(backend->spi->bus);
#endif

	rc = spi_transceive_cb(backend->spi->bus, &backend->spi->config, NULL, &rxbs, fifo_read_cb,
			       backend);
	if (rc < 0) {
		LOG_ERR("FIFO read trigger failed");
#ifdef CONFIG_MODEM_BACKEND_U_BLOX_SPI_PM_MODE_BURST
		pm_device_runtime_put(backend->spi->bus);
#endif
		k_sem_give(&backend->bus_sem);
		k_work_reschedule(&backend->fifo_read, K_MSEC(10));
	}
}

static int modem_backend_ublox_spi_open(void *data)
{
	struct modem_backend_ublox_spi *backend = data;

	LOG_DBG("Opening SPI modem backend");

#ifdef CONFIG_MODEM_BACKEND_U_BLOX_SPI_PM_MODE_ALWAYS
	pm_device_runtime_get(backend->spi->bus);
#endif
	/* Schedule the boot poll loop */
	backend->flags = MODE_BOOTING | MODE_POLLING;
	k_work_reschedule(&backend->fifo_read, K_NO_WAIT);
	return 0;
}

static int modem_backend_ublox_spi_close(void *data)
{
	struct modem_backend_ublox_spi *backend = data;
	struct k_work_sync sync;

	LOG_DBG("Closing SPI modem backend");

	/* Disable data ready interrupt */
	(void)gpio_pin_interrupt_configure_dt(backend->data_ready, GPIO_INT_DISABLE);
	(void)gpio_pin_configure_dt(backend->data_ready, GPIO_DISCONNECTED);
	/* Cancel any pending queries */
	backend->flags = MODE_CLOSED;
	k_work_cancel_delayable_sync(&backend->fifo_read, &sync);

#ifdef CONFIG_MODEM_BACKEND_U_BLOX_SPI_PM_MODE_ALWAYS
	pm_device_runtime_put(backend->spi->bus);
#endif

	/* Notify pipe closed */
	modem_pipe_notify_closed(&backend->pipe);
	return 0;
}

static int modem_backend_ublox_spi_transmit(void *data, const uint8_t *buf, size_t size)
{
	struct modem_backend_ublox_spi *backend = data;
	const struct spi_buf rx = {
		.buf = backend->spi_rx,
		.len = sizeof(backend->spi_rx),
	};
	const struct spi_buf tx = {
		.buf = (uint8_t *)buf,
		.len = size,
	};
	const struct spi_buf_set rxbs = {
		.buffers = &rx,
		.count = 1,
	};
	const struct spi_buf_set txbs = {
		.buffers = &tx,
		.count = 1,
	};
	int rc;

	if (size > sizeof(backend->spi_rx)) {
		LOG_WRN("Payload too large (%d > %d)", size, sizeof(backend->spi_rx));
		return -ENOMEM;
	}

	/* Wait for bus to be available */
	if (k_sem_take(&backend->bus_sem, K_MSEC(100)) < 0) {
		return -EAGAIN;
	}

#ifdef CONFIG_MODEM_BACKEND_U_BLOX_SPI_PM_MODE_BURST
	/* Power up SPI */
	pm_device_runtime_get(backend->spi->bus);
#endif

	LOG_HEXDUMP_DBG(buf, size, "TX");

	/* Submit TX work */
	rc = spi_transceive_cb(backend->spi->bus, &backend->spi->config, &txbs, &rxbs, fifo_read_cb,
			       backend);
	if (rc < 0) {
		LOG_ERR("FIFO read trigger failed");
	}
	return rc;
}

static int modem_backend_ublox_spi_receive(void *data, uint8_t *buf, size_t size)
{
	struct modem_backend_ublox_spi *backend = data;

	return ring_buf_get(&backend->pipe_ring_buf, buf, size);
}

static void data_ready_gpio_callback(const struct device *dev, struct gpio_callback *cb,
				     uint32_t pins)
{
	struct modem_backend_ublox_spi *backend =
		CONTAINER_OF(cb, struct modem_backend_ublox_spi, data_ready_cb);

	LOG_DBG("Data ready interrupt");
	/* Schedule the FIFO data query */
	k_work_reschedule(&backend->fifo_read, K_NO_WAIT);
}

struct modem_pipe_api modem_backend_ublox_spi_api = {
	.open = modem_backend_ublox_spi_open,
	.transmit = modem_backend_ublox_spi_transmit,
	.receive = modem_backend_ublox_spi_receive,
	.close = modem_backend_ublox_spi_close,
};

struct modem_pipe *modem_backend_ublox_spi_init(struct modem_backend_ublox_spi *backend,
						const struct modem_backend_ublox_spi_config *config)
{
	backend->spi = config->spi;
	backend->data_ready = config->data_ready;
	backend->poll_period = config->poll_period;
	backend->flags = MODE_CLOSED;
	k_poll_signal_init(&backend->read_result);
	k_poll_signal_init(&backend->read_result);
	k_work_init_delayable(&backend->fifo_read, fifo_read_trigger);
	k_sem_init(&backend->bus_sem, 1, 1);
	modem_pipe_init(&backend->pipe, backend, &modem_backend_ublox_spi_api);
	ring_buf_init(&backend->pipe_ring_buf, sizeof(backend->pipe_memory), backend->pipe_memory);
	gpio_init_callback(&backend->data_ready_cb, data_ready_gpio_callback,
			   BIT(config->data_ready->pin));
	if (gpio_add_callback(config->data_ready->port, &backend->data_ready_cb) < 0) {
		LOG_ERR("Unable to add data ready callback");
	}

	return &backend->pipe;
}

void modem_backend_ublox_spi_use_data_ready_gpio(struct modem_backend_ublox_spi *backend)
{
	/* Clear the polling bit */
	backend->flags &= ~MODE_POLLING;
	/* Enable the interrupt */
	(void)gpio_pin_configure_dt(backend->data_ready, GPIO_INPUT);
	(void)gpio_pin_interrupt_configure_dt(backend->data_ready, GPIO_INT_EDGE_TO_ACTIVE);
	/* Trigger a query immediately in case line already high */
	k_work_reschedule(&backend->fifo_read, K_NO_WAIT);
}
