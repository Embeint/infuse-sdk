/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gnss.h>
#include <zephyr/net_buf.h>
#include <zephyr/pm/device_runtime.h>

#include <infuse/gnss/ubx/defines.h>
#include <infuse/gnss/ubx/modem.h>
#include <infuse/gnss/ubx/cfg.h>
#include <infuse/gnss/ubx/protocol.h>

#ifndef CONFIG_GNSS_UBX_M10
#error Sample depends on M10 configuration
#endif /* CONFIG_GNSS_UBX_M10 */

struct ubx_modem_state {
	struct k_sem lock;
	struct ubx_msg_nav_pvt nav_pvt;
	struct ubx_msg_nav_sat_sv svs[64];
	uint8_t num_svs;
	bool screen_refresh;
};

#define ANSI_CURSOR_HOME  "\x1B[H"
#define ANSI_ERASE_SCREEN "\x1B[2J"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static int nav_pvt_cb(uint8_t message_class, uint8_t message_id, const void *payload,
		      size_t payload_len, void *user_data)
{
	const struct ubx_msg_nav_pvt *pvt = payload;
	struct ubx_modem_state *state = user_data;

	/* Update internal state */
	k_sem_take(&state->lock, K_FOREVER);
	memcpy(&state->nav_pvt, pvt, sizeof(*pvt));
	k_sem_give(&state->lock);
	return 0;
}

static int nav_sat_cb(uint8_t message_class, uint8_t message_id, const void *payload,
		      size_t payload_len, void *user_data)
{
	const struct ubx_msg_nav_sat *sat = payload;
	struct ubx_modem_state *state = user_data;
	uint8_t svs_to_use = MIN(sat->num_svs, ARRAY_SIZE(state->svs));

	if (svs_to_use < state->num_svs) {
		/* Lines have been dropped */
		state->screen_refresh = true;
	}

	/* Update internal state */
	k_sem_take(&state->lock, K_FOREVER);
	state->num_svs = svs_to_use;
	memcpy(state->svs, sat->svs, state->num_svs * sizeof(state->svs[0]));
	k_sem_give(&state->lock);
	return 0;
}

static uint32_t abs_mod(int32_t val, uint32_t mod)
{
	return (val >= 0 ? val : -val) % mod;
}

static void print_modem_state(const struct device *gnss, struct ubx_modem_state *state)
{
	const struct ubx_msg_nav_pvt *pvt = &state->nav_pvt;

	if (state->screen_refresh) {
		/* Erase the screen */
		printk(ANSI_ERASE_SCREEN);
		state->screen_refresh = false;
	}

	/* Move cursor to start */
	printk(ANSI_CURSOR_HOME);

	/* Permanent information */
	printk("%16s: %s\n", "Device", gnss->name);
	printk("%16s: %u\n", "Uptime", k_uptime_seconds());
	printk("%16s: %u\n", "ITOW", pvt->itow);

	printk("%16s: %6d.%07u\n", "Latitude", pvt->lat / 10000000, abs_mod(pvt->lat, 10000000));
	printk("%16s: %6d.%07u\n", "Longitude", pvt->lon / 10000000, abs_mod(pvt->lon, 10000000));
	printk("%16s: %6d.%03u m\n", "Height", pvt->height / 1000, abs_mod(pvt->height, 1000));
	printk("%16s: %6u.%03u m\n", "Accuracy", pvt->h_acc / 1000, pvt->h_acc % 1000);
	printk("%16s: %6u.%02d\n", "PDOP", pvt->p_dop / 100, pvt->p_dop % 100);
	printk("%16s: %6u\n", "Satellites", state->num_svs);
	printk("Constellation | SV ID | C/N0 | Quality\n");
	for (int i = 0; i < state->num_svs; i++) {
		struct ubx_msg_nav_sat_sv *sv = &state->svs[i];
		uint8_t quality = sv->flags & UBX_MSG_NAV_SAT_FLAGS_QUALITY_IND_MASK;

		printk("%13s |   %3d |   %2d | %d\n", ubx_gnss_id_name(sv->gnss_id), sv->sv_id,
		       sv->cno, quality);
	}
}

int main(void)
{
	NET_BUF_SIMPLE_DEFINE(cfg_buf, 48);
	const struct device *gnss = DEVICE_DT_GET(DT_ALIAS(gnss));
	struct ubx_modem_data *modem;
	struct ubx_modem_state state;
	int rc;

	k_sem_init(&state.lock, 1, 1);

	if (!device_is_ready(gnss)) {
		LOG_ERR("GNSS %s not ready", gnss->name);
		return -ENODEV;
	}
	modem = ubx_modem_data_get(gnss);
	state.screen_refresh = true;

	/* Power up GNSS modem */
	rc = pm_device_runtime_get(gnss);
	if (rc != 0) {
		LOG_ERR("Failed to request GNSS (%d)", rc);
		return rc;
	}

	/* Configure GNSS modem */
	ubx_msg_prepare_valset(&cfg_buf,
			       UBX_MSG_CFG_VALSET_LAYERS_RAM | UBX_MSG_CFG_VALSET_LAYERS_BBR);

	/* Core location and satellite information messages  */
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_MSGOUT_UBX_NAV_PVT_I2C, 1);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_MSGOUT_UBX_NAV_SAT_I2C, 1);
	/* Full power GNSS mode */
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_PM_OPERATEMODE, UBX_CFG_PM_OPERATEMODE_FULL);
	/* Align timepulse to GPS time */
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_TP_TIMEGRID_TP1, UBX_CFG_TP_TIMEGRID_TP1_GPS);

	ubx_msg_finalise(&cfg_buf);
	rc = ubx_modem_send_sync_acked(modem, &cfg_buf, K_MSEC(250));
	if (rc < 0) {
		LOG_WRN("Failed to configure modem");
		(void)pm_device_runtime_put(gnss);
		return rc;
	}

	struct ubx_message_handler_ctx pvt_handler_ctx = {
		.message_class = UBX_MSG_CLASS_NAV,
		.message_id = UBX_MSG_ID_NAV_PVT,
		.message_cb = nav_pvt_cb,
		.user_data = &state,
	};
	struct ubx_message_handler_ctx sat_handler_ctx = {
		.message_class = UBX_MSG_CLASS_NAV,
		.message_id = UBX_MSG_ID_NAV_SAT,
		.message_cb = nav_sat_cb,
		.user_data = &state,
	};

	/* Subscribe to NAV-SAT message */
	ubx_modem_msg_subscribe(modem, &pvt_handler_ctx);
	ubx_modem_msg_subscribe(modem, &sat_handler_ctx);

	for (;;) {
		k_sleep(K_SECONDS(1));

		/* Update display once a second */
		k_sem_take(&state.lock, K_FOREVER);
		if (k_uptime_seconds() % 10 == 0) {
			/* Periodic complete refresh to cleanup errors */
			state.screen_refresh = true;
		}
		print_modem_state(gnss, &state);
		k_sem_give(&state.lock);
	}
	return 0;
}
