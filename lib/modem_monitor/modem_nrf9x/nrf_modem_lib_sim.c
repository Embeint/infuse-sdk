/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdio.h>

#include <zephyr/logging/log.h>

#include <nrf_socket.h>
#include <nrf_modem.h>
#include <nrf_modem_at.h>
#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>
#include <modem/pdn.h>

LOG_MODULE_REGISTER(nrf_modem_sim, LOG_LEVEL_INF);

static char input_buffer[256];
static char output_buffer[256];
static nrf_modem_at_notif_handler_t at_handler;
static bool is_init;
static uint8_t rsrp_stored = 255;
static uint8_t rsrq_stored = 255;

int nrf_modem_at_sem_timeout_set(int timeout_ms)
{
	ARG_UNUSED(timeout_ms);

	return 0;
}

bool nrf_modem_is_initialized(void)
{
	return is_init;
}

int nrf_modem_lib_init(void)
{
	int err = 0;

	is_init = true;

	STRUCT_SECTION_FOREACH(nrf_modem_lib_init_cb, e) {
		LOG_DBG("Modem init callback: %p", e->callback);
		e->callback(err, e->context);
	}
	return 0;
}

int nrf_setdnsaddr(int family, const void *in_addr, nrf_socklen_t in_size)
{
	ARG_UNUSED(family);
	ARG_UNUSED(in_addr);
	ARG_UNUSED(in_size);

	return -1;
}

int nrf_inet_pton(int af, const char *src, void *dst)
{
	ARG_UNUSED(af);
	ARG_UNUSED(src);
	ARG_UNUSED(dst);

	return -1;
}

int nrf_modem_at_notif_handler_set(nrf_modem_at_notif_handler_t callback)
{
	at_handler = callback;
	return 0;
}

void nrf_modem_lib_sim_send_at(const char *msg)
{
	LOG_DBG("%s", msg);
	at_handler(msg);
}

void nrf_modem_lib_sim_signal_strength(uint8_t rsrp, uint8_t rsrq)
{
	rsrp_stored = rsrp;
	rsrq_stored = rsrq;
}

int nrf_modem_at_printf(const char *fmt, ...)
{
	va_list args;
	int out_len;

	/* Generate command */
	va_start(args, fmt);
	out_len = vsnprintf(input_buffer, sizeof(input_buffer), fmt, args);
	va_end(args);

	LOG_INF("%s: %s", __func__, input_buffer);
	return 0;
}

int nrf_modem_at_scanf(const char *cmd, const char *fmt, ...)
{
	const char *out = NULL;
	va_list args;
	int found;

	if (strstr("AT+CGMM", cmd)) {
		out = CONFIG_INFUSE_NRF_MODEM_LIB_SIM_MODEL "\nOK";
	} else if (strstr("AT+CGMR", cmd)) {
		out = CONFIG_INFUSE_NRF_MODEM_LIB_SIM_FIRMWARE "\nOK";
	} else if (strstr("AT+CGSN=0", cmd)) {
		out = CONFIG_INFUSE_NRF_MODEM_LIB_SIM_ESN "\nOK";
	} else if (strstr("AT+CGSN=1", cmd)) {
		out = "+CGSN: \"" CONFIG_INFUSE_NRF_MODEM_LIB_SIM_IMEI "\"\nOK";
	} else if (strstr("AT+CIMI", cmd)) {
		out = CONFIG_INFUSE_NRF_MODEM_LIB_SIM_IMSI "\nOK";
	} else if (strstr("AT%XICCID", cmd)) {
		out = "%XICCID: " CONFIG_INFUSE_NRF_MODEM_LIB_SIM_UICC "\nOK";
	} else if (strstr("AT+CESQ", cmd)) {
		snprintf(output_buffer, sizeof(output_buffer), "+CESQ: 99,99,255,255,%d,%d\nOK",
			 rsrq_stored, rsrp_stored);
		out = output_buffer;
	} else if (strstr("AT%XMONITOR", cmd)) {
		out = "%XMONITOR: "
		      "5,\"\",\"\",\"50501\",\"702A\",7,28,\"08C3BD0C\",103,9410,27,21,\"\","
		      "\"00001000\",\"00101101\",\"01011111\"\r\nOK\r\n";
	} else if (strstr("AT%XCONNSTAT?", cmd)) {
		out = "%XCONNSTAT: 0,0,18,6,0,0";
	}
	if (out == NULL) {
		LOG_WRN("Didn't handle %s %s", cmd, fmt);
		return 0;
	}
	LOG_INF("%s: %s\n%s", __func__, cmd, out);

	va_start(args, fmt);
	found = vsscanf(out, fmt, args);
	va_end(args);
	return found;
}

int nrf_modem_at_cmd(void *buf, size_t len, const char *fmt, ...)
{
	va_list args;
	int out_len;

	ARG_UNUSED(buf);
	ARG_UNUSED(len);

	/* Generate command */
	va_start(args, fmt);
	out_len = vsnprintf(input_buffer, sizeof(input_buffer), fmt, args);
	va_end(args);

	LOG_INF("%s: %s", __func__, input_buffer);

	return 0;
}

void lte_net_if_modem_fault_app_handler(struct nrf_modem_fault_info *fault_info);

void nrf_modem_fault_handler(struct nrf_modem_fault_info *fault_info)
{
	/* Normally this is routed through `lte_net_if.c` */
	lte_net_if_modem_fault_app_handler(fault_info);
}

static char pdn_default_apn[32];
static enum pdn_fam pdn_default_fam;

int pdn_ctx_configure(uint8_t cid, const char *apn, enum pdn_fam family, struct pdn_pdp_opt *opts)
{
	ARG_UNUSED(opts);

	if (cid != 0) {
		return 0;
	}
	strncpy(pdn_default_apn, apn, sizeof(pdn_default_apn));
	pdn_default_fam = family;
	return 0;
}

void nrf_modem_lib_sim_default_pdn_ctx(const char **apn, enum pdn_fam *family)
{
	*apn = pdn_default_apn;
	*family = pdn_default_fam;
}
