/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <infuse/version.h>

/* Defines from the build system (should match mcuboot) */
static const struct infuse_version build_version = {
	.major = APP_VERSION_MAJOR,
	.minor = APP_VERSION_MINOR,
	.revision = APP_PATCHLEVEL,
#ifdef APP_GIT_COMMIT_HASH_SHORT
	.build_num = APP_GIT_COMMIT_HASH_SHORT,
#else
	.build_num = APP_TWEAK,
#endif
};

struct infuse_version application_version_get(void)
{
#ifdef CONFIG_MCUBOOT_IMG_MANAGER
	struct mcuboot_img_header header;
	int rc;

	/* Prefer version as reported by mcuboot */
	rc = boot_read_bank_header(FIXED_PARTITION_ID(slot0_partition), &header, sizeof(header));
	if ((rc == 0) && (header.mcuboot_version == 1)) {
		return header.h.v1.sem_ver;
	}
#endif /* CONFIG_MCUBOOT_IMG_MANAGER */

	return build_version;
}
