/**
 * @file
 * @brief Emulated GNSS driver control
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_GNSS_GNSS_EMUL_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_GNSS_GNSS_EMUL_H_

#include <stdint.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief gnss_emul API
 * @defgroup gnss_emul_apis emulated GNSS APIs
 * @{
 */

/**
 * @brief Get pointers to emulated GNSS device state
 *
 * @param dev Emulated GNSS device
 * @param pm_rc Value that is returned on next PM call
 * @param comms_reset_cnt Number of times @ref ubx_modem_comms_reset has been called
 */
void emul_gnss_ubx_dev_ptrs(const struct device *dev, int **pm_rc, int **comms_reset_cnt);

/** Emulated GNSS parameters */
struct gnss_pvt_emul_location {
	int32_t latitude;
	int32_t longitude;
	int32_t height;
	uint32_t h_acc;
	uint32_t v_acc;
	uint32_t t_acc;
	uint16_t p_dop;
	uint8_t num_sv;
};

/**
 * @brief Configure the currently output PVT message
 *
 * @param dev Emulated GNSS device
 * @param emul_location Emulated location
 */
void emul_gnss_pvt_configure(const struct device *dev,
			     struct gnss_pvt_emul_location *emul_location);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_GNSS_GNSS_EMUL_H_ */
