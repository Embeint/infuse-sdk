/*
 * Copyright (c) 2024 Embeint Inc
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/__assert.h>

#include <infuse/modem/backend/u_blox_i2c.h>

LOG_MODULE_REGISTER(modem_ublox_i2c, LOG_LEVEL_INF);

RTIO_IODEV_DEFINE(i2c_iodev, &i2c_iodev_api, NULL);
RTIO_DEFINE(i2c_rtio, 4, 4);

enum {
	MODE_BOOTING = BIT(0),
	MODE_POLLING = BIT(1),
};

static void write_cb(struct rtio *r, const struct rtio_sqe *sqe, void *arg)
{
	struct modem_backend_ublox_i2c *backend = arg;
	struct rtio_cqe *wr_cqe;

	/* Release bus */
	k_sem_give(&backend->bus_sem);

	/* Can't release directly in the completion callback */
	pm_device_runtime_put_async(backend->i2c->bus, K_MSEC(10));

	wr_cqe = rtio_cqe_consume(r);
	__ASSERT_NO_MSG(wr_cqe != NULL);
	rtio_cqe_release(r, wr_cqe);

	/* Notify transmit idle */
	modem_pipe_notify_transmit_idle(&backend->pipe);
}

static void fifo_bytes_read_cb(struct rtio *r, const struct rtio_sqe *sqe, void *arg)
{
	struct modem_backend_ublox_i2c *backend = arg;
	struct rtio_cqe *wr_cqe, *rd_cqe;
	int rc = 0;

	/* Consume completion events */
	wr_cqe = rtio_cqe_consume(r);
	rd_cqe = rtio_cqe_consume(r);

	__ASSERT_NO_MSG(wr_cqe != NULL);
	__ASSERT_NO_MSG(rd_cqe != NULL);

	/* Dump received data */
	LOG_HEXDUMP_DBG(rd_cqe->userdata, backend->bytes_pending, "RX");

	if (wr_cqe->result < 0) {
		rc = wr_cqe->result;
	} else if (rd_cqe->result < 0) {
		rc = rd_cqe->result;
	}
	rtio_cqe_release(r, wr_cqe);
	rtio_cqe_release(r, rd_cqe);

	/* Finish buffer put */
	ring_buf_put_finish(&backend->pipe_ring_buf, backend->bytes_pending);

	/* Release bus */
	k_sem_give(&backend->bus_sem);

	/* Notify consumers that data exists to read */
	modem_pipe_notify_receive_ready(&backend->pipe);

	/* Release bus */
	pm_device_runtime_put_async(backend->i2c->bus, K_MSEC(10));

	/* If in interrupt driven mode and data ready pin still asserted after read, poll again */
	if (!(backend->flags & MODE_POLLING)) {
		if (gpio_pin_get_dt(backend->data_ready)) {
			LOG_DBG("Rescheduling poll");
			k_work_reschedule(&backend->fifo_read, K_NO_WAIT);
		}
	}
}

static void fifo_bytes_pending_cb(struct rtio *r, const struct rtio_sqe *sqe, void *arg)
{
	struct modem_backend_ublox_i2c *backend = arg;
	struct rtio_cqe *wr_cqe, *rd_cqe;
	bool failed = false;
	int rc;

	/* Register is in BE format */
	backend->bytes_pending = sys_be16_to_cpu(backend->bytes_pending);

	/* Consume completion events */
	wr_cqe = rtio_cqe_consume(r);
	rd_cqe = rtio_cqe_consume(r);
	__ASSERT_NO_MSG(wr_cqe != NULL);
	__ASSERT_NO_MSG(rd_cqe != NULL);
	if ((wr_cqe->result < 0) || (rd_cqe->result < 0)) {
		failed = true;
	}
	rtio_cqe_release(r, wr_cqe);
	rtio_cqe_release(r, rd_cqe);

	if (backend->flags & MODE_BOOTING) {
		if (failed) {
			LOG_DBG("Not ready yet...");
		} else {
			LOG_DBG("Modem pipe opened");
			backend->flags ^= MODE_BOOTING;
			modem_pipe_notify_opened(&backend->pipe);
		}
	}

	/* If in polling mode, or query failed */
	if ((backend->flags & MODE_POLLING) || failed) {
		/* Reschedule another data poll */
		k_work_reschedule(&backend->fifo_read, backend->poll_period);
	}
	/* If we aren't running the FIFO read */
	if (failed || (backend->bytes_pending == 0)) {
		/* Release bus */
		k_sem_give(&backend->bus_sem);
		/* Can't release directly in the completion callback */
		pm_device_runtime_put_async(backend->i2c->bus, K_MSEC(10));
		return;
	}

	uint16_t pending = backend->bytes_pending;
	struct rtio_sqe *wr_sqe, *rd_sqe, *cb_sqe;
	const uint8_t fifo_addr = 0xFF;
	uint8_t *output_buf;

	/* Limit read to buffer claim size */
	backend->bytes_pending =
		ring_buf_put_claim(&backend->pipe_ring_buf, &output_buf, backend->bytes_pending);

	LOG_DBG("Pending: %d Reading: %d", pending, backend->bytes_pending);

	/* Prepare RTIO sequence */
	wr_sqe = rtio_sqe_acquire(&i2c_rtio);
	rd_sqe = rtio_sqe_acquire(&i2c_rtio);
	cb_sqe = rtio_sqe_acquire(&i2c_rtio);

	__ASSERT_NO_MSG(wr_sqe != NULL);
	__ASSERT_NO_MSG(rd_sqe != NULL);
	__ASSERT_NO_MSG(cb_sqe != NULL);

	rtio_sqe_prep_tiny_write(wr_sqe, &i2c_iodev, RTIO_PRIO_NORM, &fifo_addr, 1, NULL);
	rtio_sqe_prep_read(rd_sqe, &i2c_iodev, 0, output_buf, backend->bytes_pending, NULL);
	rtio_sqe_prep_callback(cb_sqe, fifo_bytes_read_cb, (void *)backend, NULL);

	wr_sqe->flags |= RTIO_SQE_TRANSACTION;
	rd_sqe->flags |= RTIO_SQE_CHAINED;
	cb_sqe->flags |= RTIO_SQE_NO_RESPONSE;
	rd_sqe->userdata = output_buf;

	rd_sqe->iodev_flags |= RTIO_IODEV_I2C_STOP | RTIO_IODEV_I2C_RESTART;

	/* Submit work */
	rc = rtio_submit(&i2c_rtio, 0);
	if (rc < 0) {
		LOG_ERR("Failed to submit RTIO (%d)", rc);
		/* Release bus */
		k_sem_give(&backend->bus_sem);
		/* Can't release directly in the completion callback */
		pm_device_runtime_put_async(backend->i2c->bus, K_MSEC(10));
	}
}

static void fifo_read_start(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct modem_backend_ublox_i2c *backend =
		CONTAINER_OF(dwork, struct modem_backend_ublox_i2c, fifo_read);
	struct rtio_sqe *wr_sqe, *rd_sqe, *cb_sqe;
	const uint8_t len_addr = 0xFD;
	int rc;

	if (k_sem_take(&backend->bus_sem, K_NO_WAIT) < 0) {
		/* Bus in use, try again in 1 msec */
		k_work_reschedule(dwork, K_MSEC(1));
		return;
	}

	/* Prepare RTIO sequence */
	wr_sqe = rtio_sqe_acquire(&i2c_rtio);
	rd_sqe = rtio_sqe_acquire(&i2c_rtio);
	cb_sqe = rtio_sqe_acquire(&i2c_rtio);

	__ASSERT_NO_MSG(wr_sqe != NULL);
	__ASSERT_NO_MSG(rd_sqe != NULL);
	__ASSERT_NO_MSG(cb_sqe != NULL);

	rtio_sqe_prep_tiny_write(wr_sqe, &i2c_iodev, RTIO_PRIO_NORM, &len_addr, 1, NULL);
	rtio_sqe_prep_read(rd_sqe, &i2c_iodev, 0, (uint8_t *)&backend->bytes_pending, 2, NULL);
	rtio_sqe_prep_callback(cb_sqe, fifo_bytes_pending_cb, (void *)backend, NULL);

	wr_sqe->flags |= RTIO_SQE_TRANSACTION;
	rd_sqe->flags |= RTIO_SQE_CHAINED;
	cb_sqe->flags |= RTIO_SQE_NO_RESPONSE;

	rd_sqe->iodev_flags |= RTIO_IODEV_I2C_STOP | RTIO_IODEV_I2C_RESTART;

	/* Power up I2C */
	pm_device_runtime_get(backend->i2c->bus);

	/* Submit RTIO sequence */
	rc = rtio_submit(&i2c_rtio, 0);
	if (rc < 0) {
		LOG_ERR("Failed to submit RTIO (%d)", rc);
		k_work_reschedule(dwork, K_MSEC(100));
	}
}

static int modem_backend_ublox_i2c_open(void *data)
{
	struct modem_backend_ublox_i2c *backend = data;

	LOG_DBG("Opening I2C modem backend");

	/* Schedule the boot poll loop */
	backend->flags = MODE_BOOTING | MODE_POLLING;
	k_work_reschedule(&backend->fifo_read, K_NO_WAIT);
	return 0;
}

static int modem_backend_ublox_i2c_close(void *data)
{
	struct modem_backend_ublox_i2c *backend = data;
	struct k_work_sync sync;

	LOG_DBG("Closing I2C modem backend");

	/* Cancel any pending queries */
	k_work_cancel_delayable_sync(&backend->fifo_read, &sync);
	/* Notify pipe closed */
	modem_pipe_notify_closed(&backend->pipe);
	return 0;
}

static int modem_backend_ublox_i2c_transmit(void *data, const uint8_t *buf, size_t size)
{
	struct modem_backend_ublox_i2c *backend = data;
	struct rtio_sqe *wr_sqe, *cb_sqe;
	int rc;

	if (k_sem_take(&backend->bus_sem, K_MSEC(100)) < 0) {
		return -EAGAIN;
	}

	wr_sqe = rtio_sqe_acquire(&i2c_rtio);
	cb_sqe = rtio_sqe_acquire(&i2c_rtio);

	__ASSERT_NO_MSG(wr_sqe != NULL);
	__ASSERT_NO_MSG(cb_sqe != NULL);

	rtio_sqe_prep_write(wr_sqe, &i2c_iodev, RTIO_PRIO_NORM, (uint8_t *)buf, size, NULL);
	rtio_sqe_prep_callback(cb_sqe, write_cb, (void *)backend, NULL);

	wr_sqe->flags |= RTIO_SQE_CHAINED;
	cb_sqe->flags |= RTIO_SQE_NO_RESPONSE;
	wr_sqe->iodev_flags |= RTIO_IODEV_I2C_STOP | RTIO_IODEV_I2C_RESTART;

	/* Power up I2C */
	pm_device_runtime_get(backend->i2c->bus);

	LOG_HEXDUMP_DBG(buf, size, "TX");

	/* Submit TX work */
	rc = rtio_submit(&i2c_rtio, 0);
	return rc == 0 ? size : rc;
}

static int modem_backend_ublox_i2c_receive(void *data, uint8_t *buf, size_t size)
{
	struct modem_backend_ublox_i2c *backend = data;

	return ring_buf_get(&backend->pipe_ring_buf, buf, size);
}

static void data_ready_gpio_callback(const struct device *dev, struct gpio_callback *cb,
				     uint32_t pins)
{
	struct modem_backend_ublox_i2c *backend =
		CONTAINER_OF(cb, struct modem_backend_ublox_i2c, data_ready_cb);

	LOG_DBG("Data ready interrupt");
	/* Schedule the FIFO data query */
	k_work_reschedule(&backend->fifo_read, K_NO_WAIT);
}

struct modem_pipe_api modem_backend_ublox_i2c_api = {
	.open = modem_backend_ublox_i2c_open,
	.transmit = modem_backend_ublox_i2c_transmit,
	.receive = modem_backend_ublox_i2c_receive,
	.close = modem_backend_ublox_i2c_close,
};

struct modem_pipe *modem_backend_ublox_i2c_init(struct modem_backend_ublox_i2c *backend,
						const struct modem_backend_ublox_i2c_config *config)
{
	backend->i2c = config->i2c;
	backend->data_ready = config->data_ready;
	backend->poll_period = config->poll_period;
	i2c_iodev.data = (void *)config->i2c;
	k_poll_signal_init(&backend->read_result);
	k_poll_signal_init(&backend->read_result);
	k_work_init_delayable(&backend->fifo_read, fifo_read_start);
	k_sem_init(&backend->bus_sem, 1, 1);
	modem_pipe_init(&backend->pipe, backend, &modem_backend_ublox_i2c_api);
	ring_buf_init(&backend->pipe_ring_buf, sizeof(backend->pipe_memory), backend->pipe_memory);
	gpio_init_callback(&backend->data_ready_cb, data_ready_gpio_callback,
			   BIT(config->data_ready->pin));
	if (gpio_add_callback(config->data_ready->port, &backend->data_ready_cb) < 0) {
		LOG_ERR("Unable to add data ready callback");
	}

	return &backend->pipe;
}

void modem_backend_ublox_i2c_use_data_ready_gpio(struct modem_backend_ublox_i2c *backend)
{
	/* Clear the polling bit */
	backend->flags &= ~MODE_POLLING;
	/* Enable the interrupt */
	(void)gpio_pin_configure_dt(backend->data_ready, GPIO_INPUT);
	(void)gpio_pin_interrupt_configure_dt(backend->data_ready, GPIO_INT_EDGE_TO_ACTIVE);
	/* Trigger a query immediately in case line already high */
	k_work_reschedule(&backend->fifo_read, K_NO_WAIT);
}
