/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <infuse/identifiers.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

LOG_MODULE_REGISTER(infuse, CONFIG_INFUSE_COMMON_LOG_LEVEL);

static int infuse_common_boot(void)
{
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboot = {0};
	uint64_t device_id = infuse_device_id();

#ifdef CONFIG_KV_STORE
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboot_fallback = {0};
	int rc;

	/* Initialise KV store */
	rc = kv_store_init();

	/* Get current reboot count */
	if (rc == 0) {
		rc = kv_store_read_fallback(KV_KEY_REBOOTS, &reboot, sizeof(reboot), &reboot_fallback,
					    sizeof(reboot_fallback));
		if (rc == sizeof(reboot)) {
			/* Increment reboot counter */
			reboot.count += 1;
			(void)KV_STORE_WRITE(KV_KEY_REBOOTS, &reboot);
		}
	}
#endif /* CONFIG_KV_STORE */

	LOG_INF("\t Device: %016llx", device_id);
	LOG_INF("\t  Board: %s", CONFIG_BOARD);
	LOG_INF("\tReboots: %d", reboot.count);
	return 0;
}

SYS_INIT(infuse_common_boot, APPLICATION, 10);
