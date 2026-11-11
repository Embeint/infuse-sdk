/**
 * @file
 * @brief Bluetooth TDF helpers
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_BLUETOOTH_TDF_H_
#define INFUSE_SDK_INCLUDE_INFUSE_BLUETOOTH_TDF_H_

#include <string.h>

#include <zephyr/bluetooth/addr.h>

#include <infuse/tdf/definitions.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bluetooth TDF helpers
 * @defgroup bluetooth_tdf_apis Bluetooth TDF helpers
 * @ingroup infuse_bluetooth
 * @{
 */

/**
 * @brief Populate the TDF Bluetooth address from a Bluetooth stack structure
 *
 * @param addr Bluetooth stack address
 * @param tdf TDF address structure
 */
static inline void tdf_bt_addr_le_from_stack(const bt_addr_le_t *addr,
					     struct tdf_struct_bt_addr_le *tdf)
{
	tdf->type = addr->type;
	memcpy(tdf->val, addr->a.val, 6);
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_BLUETOOTH_TDF_H_ */
