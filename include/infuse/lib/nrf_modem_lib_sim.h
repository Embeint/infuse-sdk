/**
 * @file
 * @brief
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_LIB_NRF_MODEM_LIB_SIM_H_
#define INFUSE_SDK_INCLUDE_INFUSE_LIB_NRF_MODEM_LIB_SIM_H_

#include <modem/pdn.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Query currently configured default PDN context
 *
 * @param apn Access Point Name
 * @param family IP family
 */
void nrf_modem_lib_sim_default_pdn_ctx(const char **apn, enum pdn_fam *family);

/**
 * @brief Set reported signal strenth and quality
 *
 * @param rsrp Raw RSRP index
 * @param rsrq Raw RSRQ index
 */
void nrf_modem_lib_sim_signal_strength(uint8_t rsrp, uint8_t rsrq);

/**
 * @brief Push an AT message through the system
 *
 * @param msg AT message
 */
void nrf_modem_lib_sim_send_at(const char *msg);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_LIB_NRF_MODEM_LIB_SIM_H_ */
