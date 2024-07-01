/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

#include <infuse/version.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/tdf/tdf.h>
#include <infuse/tdf/definitions.h>

#include <infuse/task_runner/tasks/tdf_logger.h>

LOG_MODULE_REGISTER(task_tdfl, CONFIG_TASK_TDF_LOGGER_LOG_LEVEL);

void task_tdf_logger_fn(struct k_work *work)
{
	struct task_data *task = task_data_from_work(work);
	const struct task_schedule *sch = task_schedule_from_data(task);
	const struct task_tdf_logger_args *args = &sch->task_args.infuse.tdf_logger;
	uint32_t delay_ms;

	if (task_runner_task_block(&task->terminate_signal, K_NO_WAIT) == 1) {
		/* Early wake by runner to terminate */
		return;
	}

	/* Random delay when first scheduled */
	if (task->executor.workqueue.reschedule_counter == 0 && args->random_delay_ms) {
		delay_ms = sys_rand32_get() % args->random_delay_ms;
		LOG_INF("Delaying for %d ms", delay_ms);
		task_workqueue_reschedule(task, K_MSEC(delay_ms));
		return;
	}

	LOG_INF("TDFs %08X", args->tdfs);
	if (args->tdfs & TASK_TDF_LOGGER_LOG_ANNOUNCE) {
		KV_KEY_TYPE(KV_KEY_REBOOTS) reboots;
		struct infuse_version v = application_version_get();

		KV_STORE_READ(KV_KEY_REBOOTS, &reboots);
		struct tdf_announce announce = {
			.application = CONFIG_INFUSE_APPLICATION_ID,
			.version =
				{
					.major = v.major,
					.minor = v.minor,
					.revision = v.revision,
					.build_num = v.build_num,
				},
			.kv_crc = kv_store_reflect_crc(),
			.uptime = k_uptime_seconds(),
			.reboots = reboots.count,
		};

		tdf_data_logger_log(args->logger, TDF_ANNOUNCE, sizeof(announce), 0, &announce);
	}

	/* Flush the logger to transmit */
	tdf_data_logger_flush(args->logger);
}
