/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/cellular.h>
#include <zephyr/pm/device_runtime.h>

#include <infuse/validation/core.h>
#include <infuse/validation/cellular_modem.h>

#define TEST "MODEM"

static K_SEM_DEFINE(fw_queried, 0, 1);
static K_SEM_DEFINE(sim_queried, 0, 2);

static void modem_info_changed(const struct device *dev, const struct cellular_evt_modem_info *mi)
{
	char info[65];

	/* Pull the information to a local buffer */
	(void)cellular_get_modem_info(dev, mi->field, info, sizeof(info));

	/* Handle field that changed */
	switch (mi->field) {
	case CELLULAR_MODEM_INFO_IMEI:
		VALIDATION_REPORT_INFO(TEST, "%16s: %s", "Modem IMEI", info);
		break;
	case CELLULAR_MODEM_INFO_MODEL_ID:
		VALIDATION_REPORT_INFO(TEST, "%16s: %s", "Modem Model", info);
		break;
	case CELLULAR_MODEM_INFO_MANUFACTURER:
		VALIDATION_REPORT_INFO(TEST, "%16s: %s", "Manufacturer", info);
		break;
	case CELLULAR_MODEM_INFO_FW_VERSION:
		VALIDATION_REPORT_INFO(TEST, "%16s: %s", "Firmware Version", info);
		k_sem_give(&fw_queried);
		break;
	case CELLULAR_MODEM_INFO_SIM_IMSI:
		VALIDATION_REPORT_INFO(TEST, "%16s: %s", "IMSI", info);
		k_sem_give(&sim_queried);
		break;
	case CELLULAR_MODEM_INFO_SIM_ICCID:
		VALIDATION_REPORT_INFO(TEST, "%16s: %s", "ICCID", info);
		k_sem_give(&sim_queried);
		break;
	}
}

static void modem_event_cb(const struct device *dev, enum cellular_event evt, const void *payload,
			   void *user_data)
{
	ARG_UNUSED(user_data);

	switch (evt) {
	case CELLULAR_EVENT_MODEM_INFO_CHANGED:
		modem_info_changed(dev, payload);
		break;
	default:
		break;
	}
}

int infuse_validation_cellular_modem(const struct device *dev, uint8_t flags)
{
	const enum cellular_event cb_events = CELLULAR_EVENT_MODEM_INFO_CHANGED;
	int rc = 0;

	/* Register for cellular modem events */
	cellular_set_callback(dev, cb_events, modem_event_cb, NULL);

	VALIDATION_REPORT_INFO(TEST, "DEV=%s", dev->name);

	/* Power up the modem */
	rc = pm_device_runtime_get(dev);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "pm_device_runtime_get (%d)", rc);
		goto test_end;
	}

	/* Wait for the firmware version to be reported */
	if (k_sem_take(&fw_queried, K_SECONDS(60)) < 0) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to read modem firmware version");
		rc = -EIO;
		goto driver_end;
	}

	/* SIM card test */
	if (flags & VALIDATION_CELLULAR_MODEM_SIM_CARD) {
		/* Wait until the modem powers up and reports SIM card info */
		if ((k_sem_take(&sim_queried, K_SECONDS(5)) < 0) ||
		    (k_sem_take(&sim_queried, K_SECONDS(5)))) {
			VALIDATION_REPORT_ERROR(TEST, "Failed to read IMSI or ICCID");
			rc = -EIO;
		}
	}

driver_end:
	/* Small delay until powering down again */
	k_sleep(K_SECONDS(1));

	/* Power down device */
	if (pm_device_runtime_put(dev) < 0) {
		if (rc == 0) {
			VALIDATION_REPORT_ERROR(TEST, "pm_device_runtime_put");
			rc = -EIO;
		}
	}

test_end:
	if (rc == 0) {
		VALIDATION_REPORT_PASS(TEST, "PASSED");
	}
	return rc;
}
