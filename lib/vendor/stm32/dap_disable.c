/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * STM32 does not have a true debug port disable, but it does have flash readback protection.
 *
 * https://community.st.com/t5/stm32-mcus/what-option-bytes-in-stm32-are-and-how-to-use-them/ta-p/49451
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/__assert.h>

#include <zephyr/device.h>
#include <zephyr/drivers/flash/stm32_flash_api_extensions.h>

#include <infuse/reboot.h>

#define FLASH_DEV DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller))

void infuse_security_disable_dap(void)
{
	struct flash_stm32_ex_op_rdp rdp_val = {0};
	int rc;

	rc = flash_ex_op(FLASH_DEV, FLASH_STM32_EX_OP_RDP, (uintptr_t)NULL, &rdp_val);
	__ASSERT_NO_MSG(rc == 0);
	if (rdp_val.enable) {
		/* Already enabled */
		return;
	}

	rdp_val.enable = true;
	rdp_val.permanent = false;

	rc = flash_ex_op(FLASH_DEV, FLASH_STM32_EX_OP_RDP, (uintptr_t)&rdp_val, &rdp_val);
	__ASSERT_NO_MSG(rc == 0);

	/* Reboot so that configuration is applied */
	sys_reboot(SYS_REBOOT_WARM);
}
