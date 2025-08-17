/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/toolchain.h>
#include <zephyr/retention/retention.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/logging/log_ctrl.h>

#include <infuse/states.h>
#include <infuse/drivers/watchdog.h>
#include <infuse/time/epoch.h>
#include <infuse/reboot.h>

#if CONFIG_INFUSE_MEMFAULT
#include <memfault/panics/assert.h>
#endif

static const struct device *retention = DEVICE_DT_GET(DT_CHOSEN(infuse_reboot_state));

static void reboot_state_store(enum infuse_reboot_reason reason, uint32_t info1, uint32_t info2)
{
	struct infuse_reboot_state state;

	/* Populate reboot information */
	state.reason = reason;
	state.epoch_time_source = epoch_time_get_source();
	state.epoch_time = epoch_time_now();
	state.uptime = k_uptime_seconds();
	state.info.exception_basic.program_counter = info1;
	state.info.exception_basic.link_register = info2;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
	/* String truncation is expected and ok */
	strncpy(state.thread_name, _current->name, sizeof(state.thread_name));
#pragma GCC diagnostic pop

	/* Store reboot information */
	retention_write(retention, 0, (void *)&state, sizeof(state));
}

FUNC_NORETURN static void cleanup_and_reboot(void)
{
#ifdef CONFIG_NET_CONNECTION_MANAGER
	if (!k_is_in_isr()) {
		/* If not in an interrupt context, attempt to cleanly bring
		 * down all networking interfaces before rebooting.
		 */
		(void)conn_mgr_all_if_disconnect(false);
		(void)conn_mgr_all_if_down(false);
	}
#endif /* CONFIG_NET_CONNECTION_MANAGER*/
	/* Flush any logs */
	LOG_PANIC();
	/* Trigger the reboot */
	sys_reboot(SYS_REBOOT_WARM);
}

static void delayed_do_reboot(struct k_work *work)
{
	struct infuse_reboot_state state;
	/* Update the first three state values in the retention as they depend on time */
	state.epoch_time_source = epoch_time_get_source();
	state.epoch_time = epoch_time_now();
	state.uptime = k_uptime_seconds();
	retention_write(retention, 0, (void *)&state, offsetof(struct infuse_reboot_state, reason));
	/* Do the reboot */
	cleanup_and_reboot();
}

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	uint32_t pc = 0, lr = 0;

	/* Extract exception info */
	if (esf != NULL) {
		pc = esf->basic.pc;
		lr = esf->basic.lr;
	}

	/* Standard reboot process */
	infuse_reboot(reason, pc, lr);
}

void infuse_watchdog_warning(const struct device *dev, int channel_id)
{
#if CONFIG_INFUSE_MEMFAULT
	/* Feed all the watchdog channels so the hardware watchdog does not
	 * interrupt our fault handling.
	 */
	infuse_watchdog_feed_all();
	/* Run the standard Memfault crash handling */
	MEMFAULT_ASSERT_EXTRA_AND_REASON(channel_id, kMfltRebootReason_SoftwareWatchdog);
#endif /* CONFIG_INFUSE_MEMFAULT */
}

void infuse_watchdog_expired(const struct device *dev, int channel_id)
{
	uint32_t info1 = channel_id;
	uint32_t info2 = UINT32_MAX;

#if CONFIG_INFUSE_WATCHDOG
	infuse_watchdog_thread_state_lookup(channel_id, &info1, &info2);
#endif

	/* Store reboot metadata */
	reboot_state_store(INFUSE_REBOOT_HW_WATCHDOG, info1, info2);
	/* Wait for watchdog to reboot us */
	for (;;)
		;
	/* Coverage information is challenging to obtain here as QEMU doesn't
	 * like sitting in the NMI handler for long enough to dump the information,
	 * even if we were to return.
	 */
}

FUNC_NORETURN void infuse_reboot(enum infuse_reboot_reason reason, uint32_t info1, uint32_t info2)
{
	/* Store reboot metadata */
	reboot_state_store(reason, info1, info2);
	/* Do the reboot */
	cleanup_and_reboot();
}

void infuse_reboot_delayed(enum infuse_reboot_reason reason, uint32_t info1, uint32_t info2,
			   k_timeout_t delay)
{
	static struct k_work_delayable reboot_worker;

#ifdef CONFIG_INFUSE_APPLICATION_STATES
	/* Set rebooting state */
	infuse_state_set(INFUSE_STATE_REBOOTING);
#endif

	/* Init the worker */
	k_work_init_delayable(&reboot_worker, delayed_do_reboot);

	/* Store initial reboot metadata */
	reboot_state_store(reason, info1, info2);

	/* Schedule the reboot */
	k_work_schedule(&reboot_worker, delay);
}

int infuse_reboot_state_query(struct infuse_reboot_state *state)
{
	uint32_t hardware_cause;
	int rc;

	/* Check data validity */
	rc = retention_is_valid(retention);
	if (rc != 1) {
		return -ENOENT;
	}

	/* Read out the reboot state */
	rc = retention_read(retention, 0, (void *)state, sizeof(*state));
	if (rc < 0) {
		return rc;
	}

	/* Populate hardware causes (Dedicated variable due to pointer alignment) */
	if (hwinfo_get_reset_cause(&hardware_cause) < 0) {
		hardware_cause = 0;
	}
	state->hardware_reason = hardware_cause;
	(void)hwinfo_clear_reset_cause();

	/* Clear the state.
	 * Ignore errors since hardware registers have already been cleared at this point.
	 */
	(void)retention_clear(retention);
	return 0;
}
