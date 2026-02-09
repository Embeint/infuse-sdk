/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/shell/shell.h>

#include <modem/nrf_modem_lib.h>
#include <nrf_modem_at.h>

static char at_buffer[512];

static int cmd_nrf91x1_info(const struct shell *sh, size_t argc, char **argv)
{
	int rc;

	rc = nrf_modem_at_scanf("AT+CGMM", "%s", at_buffer);
	if (rc == 1) {
		shell_info(sh, "   Modem Model: %s", at_buffer);
	}
	rc = nrf_modem_at_scanf("AT+CGMR", "%s", at_buffer);
	if (rc == 1) {
		shell_info(sh, "Modem Firmware: %s", at_buffer);
	}
	rc = nrf_modem_at_scanf("AT+CGSN=0", "%s", at_buffer);
	if (rc == 1) {
		shell_info(sh, "    Modem IMEI: %s", at_buffer);
	}
	return 0;
}

static int cmd_nrf91x1_at(const struct shell *sh, size_t argc, char **argv)
{
	int rc;

	rc = nrf_modem_at_cmd(at_buffer, sizeof(at_buffer), "%s", argv[1]);
	if (rc == 0) {
		shell_info(sh, "%s", at_buffer);
	} else {
		shell_error(sh, "Command '%s' failed (%d)", argv[1], rc);
	}
	return rc;
}

struct common_test_config {
	uint32_t freq_100khz;
	int16_t power_dbm;
	uint8_t band;
};

/* Common RF test parameter parsing */
static int common_config_parse(const struct shell *sh, char **argv,
			       struct common_test_config *config, int16_t power_min,
			       int16_t power_max)
{
	uint32_t freq_khz;
	int rc = 0;

	config->band = shell_strtoul(argv[1], 0, &rc);
	if (rc) {
		shell_error(sh, "<band> Unable to parse argument to integer");
		return rc;
	}
	freq_khz = shell_strtoul(argv[2], 0, &rc);
	if (rc) {
		shell_error(sh, "<freq> Unable to parse argument to integer");
		return rc;
	}
	if ((freq_khz < 600000) || (freq_khz > 2200000)) {
		shell_error(sh, "<freq> Must be between 600 MHz and 2200 MHz (and within band "
				"frequency range)");
		return rc;
	}
	if (freq_khz % 100) {
		shell_warn(sh, "<freq> rounded down to 100 kHz multiple");
	}
	config->freq_100khz = freq_khz / 100;

	config->power_dbm = shell_strtol(argv[3], 0, &rc);
	if (rc) {
		shell_error(sh, "<power> Unable to parse argument to integer");
		return rc;
	}
	if ((config->power_dbm < power_min) || (config->power_dbm > power_max)) {
		shell_error(sh, "<power> Must be between %d dBm and %d dBm", power_min, power_max);
		return rc;
	}

	return 0;
}

static int cmd_nrf91x1_tx_test(const struct shell *sh, size_t argc, char **argv)
{
	struct common_test_config config;
	int antenna_power;
	int duration_ms;
	int rc = 0;

	/* Argument parsing */
	if (common_config_parse(sh, argv, &config, -50, 23)) {
		return -EINVAL;
	}
	duration_ms = shell_strtoul(argv[4], 0, &rc);
	if (rc) {
		shell_error(sh, "<duration> Unable to parse argument to integer");
		return rc;
	}

	snprintf(at_buffer, sizeof(at_buffer), "AT%%XRFTEST=1,1,%u,%u,%d", config.band,
		 config.freq_100khz, config.power_dbm);

	/* Output test parameters */
	shell_info(sh, "        Band: %u", config.band);
	shell_info(sh, "   Frequency: %u.%u MHz", config.freq_100khz / 10, config.freq_100khz % 10);
	shell_info(sh, "Output Power: %d dBm", config.power_dbm);
	shell_info(sh, "      AT CMD: '%s'", at_buffer);

	/* Run the test command */
	rc = nrf_modem_at_scanf(at_buffer, "%%XRFTEST: %d", &antenna_power);
	if (rc == 1) {
		shell_info(sh, "Transmission started, waiting for %u ms", duration_ms);
		k_sleep(K_MSEC(duration_ms));
		shell_info(sh, "Disabling transmission");
	}

	/* Disable the TX test */
	return nrf_modem_at_printf("AT%%XRFTEST=1,0");
}

static int cmd_nrf91x1_rx_test(const struct shell *sh, size_t argc, char **argv)
{
	struct common_test_config config;
	int antenna_power, headroom;
	int power_dbm;
	int mode;
	int rc = 0;

	/* Argument parsing */
	if (common_config_parse(sh, argv, &config, -127, -25)) {
		return -EINVAL;
	}
	if (strcmp("lte-m", argv[4]) == 0) {
		mode = 1;
	} else if (strcmp("nb-iot", argv[4]) == 0) {
		mode = 0;
	} else {
		shell_error(sh, "<mode> must be one of [lte-m,nb-iot]");
		return -EINVAL;
	}

	snprintf(at_buffer, sizeof(at_buffer), "AT%%XRFTEST=0,1,%u,%u,%d,%u", config.band,
		 config.freq_100khz, config.power_dbm, mode);

	/* Output test parameters */
	shell_info(sh, "       Band: %u", config.band);
	shell_info(sh, "  Frequency: %u.%u MHz", config.freq_100khz / 10, config.freq_100khz % 10);
	shell_info(sh, "Input Power: %d dBm", config.power_dbm);
	shell_info(sh, "       Mode: %s", mode ? "LTE-M" : "NB-IoT");
	shell_info(sh, "     AT CMD: '%s'", at_buffer);

	/* Run the test command */
	rc = nrf_modem_at_scanf(at_buffer, "%%XRFTEST: %d,%d", &antenna_power, &headroom);
	if (rc == 2) {
		power_dbm = (-10 * antenna_power) / 255;
		shell_info(sh, "Results:");
		shell_info(sh, "\tAntenna power: -%d.%d dBm", power_dbm / 10, power_dbm % 10);
		shell_info(sh, "\t     Headroom: %d dBFS", headroom);
	} else {
		shell_error(sh, "Failed to measure RX signal (%d)", rc);
	}

	/* Disable the RX test */
	return nrf_modem_at_printf("AT%%XRFTEST=0,0");
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_nrf91, SHELL_CMD(info, NULL, "Display nRF91x1 modem info\n", cmd_nrf91x1_info),
	SHELL_CMD_ARG(at, NULL, "Run arbitrary AT commands\n", cmd_nrf91x1_at, 2, 0),
	SHELL_CMD_ARG(tx_test, NULL,
		      SHELL_HELP("LTE modem transmission test (Carrier Wave)",
				 "[band <3GPP band number>] "
				 "[frequency <kHz>] [output power <dBm>] [duration <ms>]"),
		      cmd_nrf91x1_tx_test, 5, 0),
	SHELL_CMD_ARG(rx_test, NULL,
		      SHELL_HELP("LTE modem reception test",
				 "[band <3GPP band number>] "
				 "[frequency <kHz>] [signal power <dBm>] [mode <lte-m,nb-iot>]"),
		      cmd_nrf91x1_rx_test, 5, 0),
	SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_CMD_REGISTER(nrf91, &sub_nrf91, "nRF91 EMC commands", NULL);
