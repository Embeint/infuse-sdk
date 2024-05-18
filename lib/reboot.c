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
#include <zephyr/logging/log_ctrl.h>

#include <infuse/time/civil.h>
#include <infuse/reboot.h>

static const struct device *retention = DEVICE_DT_GET(DT_CHOSEN(infuse_reboot_state));

static void reboot_state_store(enum infuse_reboot_reason reason, uint32_t info1, uint32_t info2)
{
	struct infuse_reboot_state state;

	/* Populate reboot information */
	state.reason = reason;
	state.civil_time_source = civil_time_get_source();
	state.civil_time = civil_time_now();
	state.uptime = k_uptime_seconds();
	state.param_1.program_counter = info1;
	state.param_2.link_register = info2;
	strncpy(state.thread_name, _current->name, sizeof(state.thread_name));

	/* Store reboot information */
	retention_write(retention, 0, (void *)&state, sizeof(state));
}

static void do_reboot(struct k_work *work)
{
	struct infuse_reboot_state state;
	/* Update the first three state values in the retention as they depend on time */
	state.civil_time_source = civil_time_get_source();
	state.civil_time = civil_time_now();
	state.uptime = k_uptime_seconds();
	retention_write(retention, 0, (void *)&state, offsetof(struct infuse_reboot_state, reason));
	/* Trigger the reboot */
	sys_reboot(SYS_REBOOT_WARM);
}

void k_sys_fatal_error_handler(unsigned int reason, const z_arch_esf_t *esf)
{
	uint32_t pc = 0, lr = 0;

	/* Extract exception info */
	if (esf != NULL) {
		pc = esf->basic.pc;
		lr = esf->basic.lr;
	}
	/* Store reboot metadata */
	reboot_state_store(reason, pc, lr);
	/* Flush any logs */
	LOG_PANIC();
	/* Reboot */
	sys_reboot(SYS_REBOOT_WARM);
}

FUNC_NORETURN void infuse_reboot(enum infuse_reboot_reason reason, uint32_t info1, uint32_t info2)
{
	/* Store reboot metadata */
	reboot_state_store(reason, info1, info2);
	/* Trigger the reboot */
	sys_reboot(SYS_REBOOT_WARM);
}

void infuse_reboot_delayed(enum infuse_reboot_reason reason, uint32_t info1, uint32_t info2,
			   k_timeout_t delay)
{
	static struct k_work_delayable reboot_worker;

	/* Init the worker */
	k_work_init_delayable(&reboot_worker, do_reboot);

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
	if (retention_is_valid(retention) != 1) {
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

	/* Clear the state */
	return retention_clear(retention);
}
