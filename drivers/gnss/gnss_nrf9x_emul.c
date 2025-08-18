/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gnss.h>

#include <nrf_errno.h>
#include <nrf_modem_gnss.h>
#include <modem/lte_lc.h>

#include <infuse/drivers/gnss/gnss_emul.h>

#define DT_DRV_COMPAT nordic_nrf9x_gnss_emul

#define SUPPORTED_SYSTEMS (GNSS_SYSTEM_GPS | GNSS_SYSTEM_QZSS)

struct nrf9x_data {
	nrf_modem_gnss_event_handler_type_t handler;
	struct nrf_modem_gnss_pvt_data_frame pvt_frame;
	struct k_work_delayable worker;
	gnss_systems_t systems;
	k_ticks_t latest_timepulse;
	uint64_t next_interrupt;
	uint16_t interval;
};

static struct nrf9x_data nrf9x_inst_data;

void emul_gnss_pvt_configure(const struct device *dev, struct gnss_pvt_emul_location *emul_location)
{
	struct nrf_modem_gnss_pvt_data_frame *f = &nrf9x_inst_data.pvt_frame;

	f->latitude = (double)emul_location->latitude / 1e7;
	f->longitude = (double)emul_location->longitude / 1e7;
	f->altitude = (float)emul_location->height / 1e3f;
	f->accuracy = (float)emul_location->h_acc / 1e3f;
	f->altitude_accuracy = (float)emul_location->v_acc / 1e3f;
	f->pdop = (float)emul_location->p_dop / 10.0f;
	f->hdop = (float)emul_location->p_dop / 10.0f;
	/* Not a real conversion, but should work for our purposes */
	f->tdop = (float)emul_location->t_acc / 1000.0f;

	if (emul_location->t_acc) {
		f->datetime.year = 2025;
		f->datetime.month = 2;
		f->datetime.day = 1;
		f->datetime.hour = 5;
	}

	for (int i = 0; i < ARRAY_SIZE(f->sv); i++) {
		f->sv[i].flags =
			(i < emul_location->num_sv) ? NRF_MODEM_GNSS_SV_FLAG_USED_IN_FIX : 0;
	}
}

static void nrf9x_interrupt_generator(struct k_work *work)
{
	static int cnt;

	if (cnt++ == 0) {
		/* Send the AGNSS request as the first event */
		nrf9x_inst_data.handler(NRF_MODEM_GNSS_EVT_AGNSS_REQ);
	}

	if (nrf9x_inst_data.pvt_frame.tdop < 1000) {
		nrf9x_inst_data.latest_timepulse = k_uptime_ticks();
	}

	nrf9x_inst_data.handler(NRF_MODEM_GNSS_EVT_PVT);
	nrf9x_inst_data.next_interrupt += 1000;
	k_work_reschedule(&nrf9x_inst_data.worker,
			  K_TIMEOUT_ABS_MS(nrf9x_inst_data.next_interrupt));
}

int lte_lc_func_mode_set(enum lte_lc_func_mode mode)
{
	return 0;
}

int32_t nrf_modem_gnss_event_handler_set(nrf_modem_gnss_event_handler_type_t handler)
{
	nrf9x_inst_data.handler = handler;
	return 0;
}

int32_t nrf_modem_gnss_use_case_set(uint8_t use_case)
{
	ARG_UNUSED(use_case);

	return 0;
}

int32_t nrf_modem_gnss_fix_interval_set(uint16_t fix_interval)
{
	nrf9x_inst_data.interval = fix_interval;
	return 0;
}

int32_t nrf_modem_gnss_start(void)
{
	nrf9x_inst_data.next_interrupt = k_uptime_get();
	k_work_schedule(&nrf9x_inst_data.worker, K_NO_WAIT);
	return 0;
}

int32_t nrf_modem_gnss_stop(void)
{
	k_work_cancel_delayable(&nrf9x_inst_data.worker);
	return 0;
}

int32_t nrf_modem_gnss_read(void *buf, int32_t buf_len, int type)
{
	uint32_t uptime = k_uptime_seconds();

	if (type != NRF_MODEM_GNSS_DATA_PVT) {
		return -NRF_ENOMSG;
	}
	if (buf_len < sizeof(nrf9x_inst_data.pvt_frame)) {
		return -NRF_EMSGSIZE;
	}

	if (nrf9x_inst_data.pvt_frame.datetime.year) {
		nrf9x_inst_data.pvt_frame.datetime.minute = uptime / 60;
		nrf9x_inst_data.pvt_frame.datetime.seconds = uptime % 60;
		nrf9x_inst_data.pvt_frame.datetime.ms = 123;
	}

	memcpy(buf, &nrf9x_inst_data.pvt_frame, sizeof(nrf9x_inst_data.pvt_frame));
	return 0;
}

static int emul_set_enabled_systems(const struct device *dev, gnss_systems_t systems)
{
	struct nrf9x_data *data = dev->data;

	data->systems = systems & SUPPORTED_SYSTEMS;
	return 0;
}

static int emul_get_enabled_systems(const struct device *dev, gnss_systems_t *systems)
{
	struct nrf9x_data *data = dev->data;

	*systems = data->systems;
	return 0;
}

static int emul_get_supported_systems(const struct device *dev, gnss_systems_t *systems)
{
	ARG_UNUSED(dev);

	*systems = SUPPORTED_SYSTEMS;
	return 0;
}

static int emul_get_latest_timepulse(const struct device *dev, k_ticks_t *timestamp)
{
	struct nrf9x_data *data = dev->data;

	k_ticks_t now = k_uptime_ticks();
	k_ticks_t max_age = (3 * CONFIG_SYS_CLOCK_TICKS_PER_SEC) / 2;
	k_ticks_t tp_age = now - data->latest_timepulse;

	if (tp_age > max_age) {
		/* Timepulse has not occurred in last 1.5 seconds, no longer valid */
		data->latest_timepulse = 0;
		return -EAGAIN;
	}
	*timestamp = data->latest_timepulse;
	return 0;
}

static int nrf9x_gnss_init(const struct device *dev)
{
	nrf9x_inst_data.systems = GNSS_SYSTEM_GPS | GNSS_SYSTEM_QZSS;
	k_work_init_delayable(&nrf9x_inst_data.worker, nrf9x_interrupt_generator);
	return 0;
}

const struct gnss_driver_api emul_gnss_api = {
	.set_enabled_systems = emul_set_enabled_systems,
	.get_enabled_systems = emul_get_enabled_systems,
	.get_supported_systems = emul_get_supported_systems,
	.get_latest_timepulse = emul_get_latest_timepulse,
};

#define NRF9X_GNSS_INST(inst)                                                                      \
	DEVICE_DT_INST_DEFINE(inst, nrf9x_gnss_init, NULL, &nrf9x_inst_data, NULL, POST_KERNEL,    \
			      CONFIG_SENSOR_INIT_PRIORITY, &emul_gnss_api);

DT_INST_FOREACH_STATUS_OKAY(NRF9X_GNSS_INST);
