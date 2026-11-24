/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * The debug lock is applied by the Secure Engine. The lock is recoverable, as device erase is
 * left enabled, which erases the main flash and unlocks the debug port.
 */

#include <zephyr/sys/reboot.h>
#include <zephyr/sys/__assert.h>

#include <sl_se_manager.h>
#include <sl_se_manager_util.h>

void infuse_security_disable_dap(void)
{
	sl_se_command_context_t cmd_ctx;
	sl_se_debug_status_t status;
	sl_status_t rc;

	rc = sl_se_init();
	__ASSERT_NO_MSG(rc == SL_STATUS_OK);
	rc = sl_se_init_command_context(&cmd_ctx);
	__ASSERT_NO_MSG(rc == SL_STATUS_OK);

	rc = sl_se_get_debug_lock_status(&cmd_ctx, &status);
	__ASSERT_NO_MSG(rc == SL_STATUS_OK);
	if ((rc == SL_STATUS_OK) && status.debug_port_lock_applied) {
		/* Debug lock already applied */
		(void)sl_se_deinit_command_context(&cmd_ctx);
		return;
	}

	rc = sl_se_apply_debug_lock(&cmd_ctx);
	__ASSERT_NO_MSG(rc == SL_STATUS_OK);
	(void)sl_se_deinit_command_context(&cmd_ctx);
	if (rc != SL_STATUS_OK) {
		return;
	}

	/* Reboot so that configuration is applied */
	sys_reboot(SYS_REBOOT_WARM);
}
