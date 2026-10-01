/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>

#include <zephyr/llext/llext.h>
#include <zephyr/llext/loader.h>
#include <zephyr/sys/util.h>

#include <infuse/algorithms/implementation.h>
#include <infuse/fs/littlefs.h>

#include "llext_littlefs_loader.h"

struct llext_littlefs_loader {
	struct llext_loader loader;
	uint32_t file;
	bool open;
};

static int littlefs_prepare(struct llext_loader *loader)
{
	struct llext_littlefs_loader *littlefs_loader =
		CONTAINER_OF(loader, struct llext_littlefs_loader, loader);
	int rc;

	rc = infuse_littlefs_file_open(INFUSE_LFS_FOLDER_ALGORITHMS, littlefs_loader->file);
	if (rc == 0) {
		littlefs_loader->open = true;
	}
	return rc;
}

static int littlefs_read(struct llext_loader *loader, void *buffer, size_t len)
{
	struct llext_littlefs_loader *littlefs_loader =
		CONTAINER_OF(loader, struct llext_littlefs_loader, loader);
	int rc;

	if (!littlefs_loader->open) {
		return -EINVAL;
	}
	rc = infuse_littlefs_file_read(buffer, len);
	return rc == len ? 0 : (rc < 0 ? rc : -EINVAL);
}

static int littlefs_seek(struct llext_loader *loader, size_t position)
{
	struct llext_littlefs_loader *littlefs_loader =
		CONTAINER_OF(loader, struct llext_littlefs_loader, loader);
	int rc;

	if (!littlefs_loader->open) {
		return -EINVAL;
	}
	rc = infuse_littlefs_file_seek(position);
	return rc == position ? 0 : (rc < 0 ? rc : -EINVAL);
}

static void littlefs_finalize(struct llext_loader *loader)
{
	struct llext_littlefs_loader *littlefs_loader =
		CONTAINER_OF(loader, struct llext_littlefs_loader, loader);

	if (littlefs_loader->open) {
		(void)infuse_littlefs_file_close();
		littlefs_loader->open = false;
	}
}

int llext_littlefs_load(uint32_t file, struct llext **extension,
			const struct infuse_algorithm **algorithm)
{
	struct llext_littlefs_loader littlefs_loader = {
		.loader =
			{
				.prepare = littlefs_prepare,
				.read = littlefs_read,
				.seek = littlefs_seek,
				.finalize = littlefs_finalize,
				.storage = LLEXT_STORAGE_TEMPORARY,
			},
		.file = file,
	};
	struct llext_load_param load_param = LLEXT_LOAD_PARAM_DEFAULT;
	char name[9];
	int rc;

	(void)snprintf(name, sizeof(name), "%08x", file);
	rc = llext_load(&littlefs_loader.loader, name, extension, &load_param);
	if (rc < 0) {
		return rc;
	}

	*algorithm = llext_find_sym(&(*extension)->exp_tab, "algorithm_config");
	if ((*algorithm == NULL) || ((*algorithm)->struct_version != 0) ||
	    ((*algorithm)->algorithm_id != file)) {
		(void)llext_unload(extension);
		return -EINVAL;
	}

	return 0;
}
