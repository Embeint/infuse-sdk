/**
 * @file
 * @brief e-peas AEM15820 sensor channels
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_SENSOR_AEM15820_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_SENSOR_AEM15820_H_

#include <zephyr/drivers/sensor.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief e-peas AEM15820 sensor channels
 * @defgroup aem15820_sensor_apis AEM15820 sensor channels
 * @ingroup infuse_drivers
 *
 * Standard channels supported by the sensor child device:
 *   - @a SENSOR_CHAN_VOLTAGE / @a SENSOR_CHAN_GAUGE_VOLTAGE: storage element (STO) voltage
 *   - @a SENSOR_CHAN_GAUGE_TEMP: thermistor temperature (only when the charger sibling has
 *     @a ntc-r0)
 *
 * The APM power channels report the average power over the last APM window (or accumulated
 * windows), converted with the alpha factor of the monitor. They are only supported when the
 * monitor is enabled on the charger sibling and the alpha factor is set on the sensor node.
 * A channel returns -ERANGE if the APM counter overflowed and -EIO if it was corrupted.
 * @{
 */

/** AEM15820 custom sensor channels */
enum aem15820_sensor_channel {
	/** Source (SRC) voltage, as measured by the last MPPT evaluation */
	SENSOR_CHAN_AEM15820_SRC_VOLTAGE = SENSOR_CHAN_PRIV_START,
	/** Average power into the storage element through the low-power boost (mW) */
	SENSOR_CHAN_AEM15820_BOOST_LP_POWER,
	/** Average power into the storage element through the high-power boost (mW) */
	SENSOR_CHAN_AEM15820_BOOST_HP_POWER,
	/** Average power from the storage element to the LOAD output (mW) */
	SENSOR_CHAN_AEM15820_LOAD_POWER,
};

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_SENSOR_AEM15820_H_ */
