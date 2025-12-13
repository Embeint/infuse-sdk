/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/posix/arpa/inet.h>
#include <zephyr/net_buf.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/sntp.h>
#include <zephyr/net/wifi_mgmt.h>

#include <infuse/net/dns.h>

#include <infuse/validation/core.h>
#include <infuse/validation/nrf70_radio_test.h>

#define TEST "NRF70"

#include "fmac_main.h"
#include "radio_test/fmac_structs.h"
#include "radio_test/fmac_api.h"

extern struct nrf_wifi_drv_priv_zep rpu_drv_priv_zep;
struct nrf_wifi_ctx_zep *ctx = &rpu_drv_priv_zep.rpu_ctx_zep;

enum nrf_wifi_status nrf_wifi_radio_test_conf_init(struct rpu_conf_params *conf_params,
						   unsigned int channel)
{
	enum nrf_wifi_status status;

	conf_params->op_mode = RPU_OP_MODE_RADIO_TEST;

	status = nrf_wifi_rt_fmac_rf_params_get(
		ctx->rpu_ctx, (struct nrf_wifi_phy_rf_params *)conf_params->rf_params);
	if (status != NRF_WIFI_STATUS_SUCCESS) {
		VALIDATION_REPORT_ERROR(TEST, "nrf_wifi_rt_fmac_rf_params_get");
		return status;
	}

	conf_params->tx_pkt_nss = 1;
	conf_params->tx_pkt_gap_us = 0;
	conf_params->tx_power = 30;
	conf_params->chan.primary_num = channel;
	conf_params->tx_mode = 1;
	conf_params->tx_pkt_num = -1;
	conf_params->tx_pkt_len = 1400;
	conf_params->tx_pkt_preamble = 0;
	conf_params->tx_pkt_rate = 6;
	conf_params->he_ltf = 2;
	conf_params->he_gi = 2;
	conf_params->aux_adc_input_chain_id = 1;
	conf_params->ru_tone = 26;
	conf_params->ru_index = 1;
	conf_params->tx_pkt_cw = 15;
	conf_params->phy_calib = NRF_WIFI_DEF_PHY_CALIB;

	memcpy(conf_params->country_code, "00", NRF_WIFI_COUNTRY_CODE_LEN);
	return NRF_WIFI_STATUS_SUCCESS;
}

static int validation_xo_calibrate(const struct device *dev, uint8_t channel)
{
	struct rpu_conf_params conf_params;
	enum nrf_wifi_status status;

	status = nrf_wifi_radio_test_conf_init(&conf_params, channel);
	if (status != NRF_WIFI_STATUS_SUCCESS) {
		return -EINVAL;
	}

	status = nrf_wifi_rt_fmac_radio_test_init(ctx->rpu_ctx, &conf_params);
	if (status != NRF_WIFI_STATUS_SUCCESS) {
		VALIDATION_REPORT_ERROR(TEST, "nrf_wifi_rt_fmac_radio_test_init");
		return -EIO;
	}

	VALIDATION_REPORT_INFO(TEST, "Starting XO calibration process");
	status = nrf_wifi_rt_fmac_rf_test_compute_xo(ctx->rpu_ctx);
	if (status != NRF_WIFI_STATUS_SUCCESS) {
		VALIDATION_REPORT_ERROR(TEST, "nrf_wifi_rt_fmac_rf_test_compute_xo");
		return -EIO;
	}
	VALIDATION_REPORT_INFO(TEST, "XO calibration process complete");

	return 0;
}

int infuse_validation_nrf70_radio_test(const struct device *dev, uint8_t flags, uint8_t channel)
{
	unsigned int timeout = 0;
	int rc = 0;

	VALIDATION_REPORT_INFO(TEST, "IFACE=%s", dev->name);

	/* Wait for driver to finish initialising */
	while (!ctx->rpu_ctx && timeout < 5000) {
		k_sleep(K_MSEC(100));
		timeout += 100;
	}
	if (!ctx->rpu_ctx) {
		VALIDATION_REPORT_ERROR(TEST, "Timed out waiting for driver");
		rc = -ETIMEDOUT;
		goto end;
	}

	if (flags & VALIDATION_NRF70_RADIO_TEST_XO_TUNE) {
		rc = validation_xo_calibrate(dev, channel);
	}

end:
	if (rc == 0) {
		VALIDATION_REPORT_PASS(TEST, "PASSED");
	}
	return rc;
}
