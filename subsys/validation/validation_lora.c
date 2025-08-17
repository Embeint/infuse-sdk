/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/util.h>

#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <infuse/validation/core.h>
#include <infuse/validation/lora.h>

#define TEST "LORA"

int infuse_validation_lora(const struct device *dev, uint8_t flags)
{
	/* Match default parameters from zephyr/samples/drivers/lora for RX compatibility */
	struct lora_modem_config config = {
		.frequency = 865100000,
		.bandwidth = BW_125_KHZ,
		.datarate = SF_10,
		.preamble_len = 8,
		.coding_rate = CR_4_5,
		.iq_inverted = false,
		.public_network = false,
		.tx_power = 30,
	};
	char tx_data[] = "validation";
	uint8_t rx_data[32];
	int16_t rssi;
	int8_t snr;
	int rc = 0;

	VALIDATION_REPORT_INFO(TEST, "DEV=%s", dev->name);

	/* Check init succeeded */
	if (!device_is_ready(dev)) {
		VALIDATION_REPORT_ERROR(TEST, "Device not ready");
		return -ENODEV;
	}

	if (flags & VALIDATION_LORA_TX) {
		/* Configure the modem for transmission */
		config.tx = true;
		rc = lora_config(dev, &config);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "TX Config failed (%d)", rc);
			goto test_end;
		}
		/* Send a packet */
		VALIDATION_REPORT_VALUE(TEST, "TX_PAYLOAD", "%s", tx_data);
		rc = lora_send(dev, tx_data, sizeof(tx_data));
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "Send failed (%d)", rc);
			goto test_end;
		}
	}

	if (flags & VALIDATION_LORA_CAD) {
		config.tx = false;
		rc = lora_config(dev, &config);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "RX Config failed (%d)", rc);
			goto test_end;
		}

		/* Run CAD */
		VALIDATION_REPORT_INFO(TEST, "Starting CAD");
		rc = lora_cad(dev, 2);
		if (rc == -ENOTSUP) {
			VALIDATION_REPORT_INFO(TEST, "CAD not supported");
		} else if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "CAD failed (%d)", rc);
			goto test_end;
		} else {
			VALIDATION_REPORT_VALUE(TEST, "CAD_RESULT", "%d", rc);
		}
	}

	if (flags & VALIDATION_LORA_RX) {
		config.tx = false;
		rc = lora_config(dev, &config);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "RX Config failed (%d)", rc);
			goto test_end;
		}
		/* Receive a packet */
		VALIDATION_REPORT_INFO(TEST, "Waiting for packet");
		rc = lora_recv(dev, rx_data, sizeof(rx_data), K_SECONDS(5), &rssi, &snr);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "Failed to receive packet (%d)", rc);
			goto test_end;
		}
		/* Report parameters */
		VALIDATION_REPORT_VALUE(TEST, "RX_LEN", "%d", rc);
		VALIDATION_REPORT_VALUE(TEST, "RX_RSSI", "%d", rssi);
		VALIDATION_REPORT_VALUE(TEST, "RX_SNR", "%d", snr);
		rc = 0;
	}
test_end:
	if (rc == 0) {
		VALIDATION_REPORT_PASS(TEST, "DEV=%s", dev->name);
	}

	return rc;
}
