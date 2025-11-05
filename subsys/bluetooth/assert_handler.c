/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>

#include <infuse/reboot.h>

#ifdef CONFIG_MEMFAULT
#include <memfault/core/reboot_reason_types.h>
#include <memfault/core/trace_event.h>
#include <memfault/panics/assert.h>
#endif /* CONFIG_MEMFAULT */

void bt_ctlr_assert_handle(char *file, uint32_t line)
{
#ifdef CONFIG_MEMFAULT
	/* Store the assert information with Memfault if enabled */
	MEMFAULT_TRACE_EVENT_WITH_LOG(bt_ctlr_fault, "%s:%u", file, line);
	/* Assert so we get a backtrace */
	MEMFAULT_ASSERT_WITH_REASON(line, kMfltRebootReason_Assert);
	/* If that didn't work for whatever reason, reboot manually */
#endif /* CONFIG_MEMFAULT */
	/* Trigger a reboot */
	infuse_reboot(INFUSE_REBOOT_BT_CTLR_FAULT, (uintptr_t)file, line);
}
