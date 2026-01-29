/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <errno.h>

#include <lfs.h>

int lfs_to_errno(int error)
{
	if (error >= 0) {
		return error;
	}

	switch (error) {
	default:
	case LFS_ERR_IO: /* Error during device operation */
		return -EIO;
	case LFS_ERR_CORRUPT: /* Corrupted */
		return -EFAULT;
	case LFS_ERR_NOENT: /* No directory entry */
		return -ENOENT;
	case LFS_ERR_EXIST: /* Entry already exists */
		return -EEXIST;
	case LFS_ERR_NOTDIR: /* Entry is not a dir */
		return -ENOTDIR;
	case LFS_ERR_ISDIR: /* Entry is a dir */
		return -EISDIR;
	case LFS_ERR_NOTEMPTY: /* Dir is not empty */
		return -ENOTEMPTY;
	case LFS_ERR_BADF: /* Bad file number */
		return -EBADF;
	case LFS_ERR_FBIG: /* File too large */
		return -EFBIG;
	case LFS_ERR_INVAL: /* Invalid parameter */
		return -EINVAL;
	case LFS_ERR_NOSPC: /* No space left on device */
		return -ENOSPC;
	case LFS_ERR_NOMEM: /* No more memory available */
		return -ENOMEM;
	}
}

int errno_to_lfs(int error)
{
	if (error >= 0) {
		return LFS_ERR_OK;
	}

	switch (error) {
	default:
	case -EIO: /* Error during device operation */
		return LFS_ERR_IO;
	case -EFAULT: /* Corrupted */
		return LFS_ERR_CORRUPT;
	case -ENOENT: /* No directory entry */
		return LFS_ERR_NOENT;
	case -EEXIST: /* Entry already exists */
		return LFS_ERR_EXIST;
	case -ENOTDIR: /* Entry is not a dir */
		return LFS_ERR_NOTDIR;
	case -EISDIR: /* Entry is a dir */
		return LFS_ERR_ISDIR;
	case -ENOTEMPTY: /* Dir is not empty */
		return LFS_ERR_NOTEMPTY;
	case -EBADF: /* Bad file number */
		return LFS_ERR_BADF;
	case -EFBIG: /* File too large */
		return LFS_ERR_FBIG;
	case -EINVAL: /* Invalid parameter */
		return LFS_ERR_INVAL;
	case -ENOSPC: /* No space left on device */
		return LFS_ERR_NOSPC;
	case -ENOMEM: /* No more memory available */
		return LFS_ERR_NOMEM;
	}
}
