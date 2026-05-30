/**
 * @file
 * @brief Infuse-IoT LittleFS wrapper
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_FS_LITTLEFS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_FS_LITTLEFS_H_

#include <stdint.h>

#include <zephyr/sys/slist.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Infuse-IoT LittleFS wrapper
 * @defgroup infuse_littlefs_apis Infuse-IoT LittleFS wrapper
 * @{
 */

/** Infuse-IoT defined filesystem folders */
enum infuse_littlefs_folder {
	/** Folder for singleton files that don't fit in other locations */
	INFUSE_LFS_FOLDER_GENERAL = 0,
	/** Folder for dynamic algorithms */
	INFUSE_LFS_FOLDER_ALGORITHMS = 1,
	/** Folder for GNSS assistance data */
	INFUSE_LFS_FOLDER_A_GNSS = 2,
	/** Folder for files to copy to other devices */
	INFUSE_LFS_FOLDER_COPY = 3,
};

#define INFUSE_LITTLEFS_METADATA_TYPE 0x1F

/** Metadata associated with all files */
struct infuse_littlefs_metadata {
	/** Creation timestamp */
	uint32_t timestamp;
	/** Opaque identifier for cloud */
	uint32_t identifier;
	/** Data validation */
	uint32_t crc;
} __packed;

/** Runtime state of the filesystem */
struct infuse_littlefs_fs_info {
	/** On-disk filesystem version */
	uint32_t disk_version;
	/** Size of filesystem blocks in bytes */
	uint32_t block_size;
	/** Total number of blocks in the filesystem */
	uint32_t block_count;
	/** Blocks currently in use by the filesystem */
	uint32_t blocks_used;
};

/** @brief Infuse LittleFS callback structure. */
struct infuse_littlefs_cb {
	/**
	 * @brief File created in the filesystem
	 *
	 * @param folder Folder containing the file
	 * @param file Filename inside the folder
	 * @param meta Metadata associated with the file
	 */
	void (*file_created)(enum infuse_littlefs_folder folder, uint32_t file,
			     struct infuse_littlefs_metadata *meta, void *user_data);

	/**
	 * @brief File deleted from the filesystem
	 *
	 */
	void (*file_deleted)(enum infuse_littlefs_folder folder, uint32_t file, void *user_data);

	/** User provided context pointer */
	void *user_data;

	sys_snode_t node;
};

/**
 * @brief Initialise and mount the LittleFS filesystem
 *
 * @retval 0 on success
 * @retval -errno negative error code on failure
 */
int infuse_littlefs_init(void);

/**
 * @brief LittleFS filesystem info
 *
 * @param info Output storage for filesystem information
 *
 * @retval 0 on success
 * @retval -EAGAIN filesystem not mounted
 * @retval -EINVAL file open for reading or writing
 * @retval -errno other negative error code on failure
 */
int infuse_littlefs_fs_info(struct infuse_littlefs_fs_info *info);

/**
 * @brief Register to be notified of LittleFS filesystem events
 *
 * @param cb Callback struct to register
 */
void infuse_littlefs_register_cb(struct infuse_littlefs_cb *cb);

#ifdef CONFIG_ZTEST

/**
 * @brief Reset internal state
 */
void infuse_littlefs_reset(void);

#endif /* CONFIG_ZTEST */

/**
 * @brief File size
 *
 * @param folder Folder containing the file
 * @param file Filename inside the folder
 *
 * @retval >=0 size of the file in bytes
 * @retval -EAGAIN filesystem not mounted
 * @retval -ENOENT file does not exist
 * @retval -errno other negative error code on failure
 */
int infuse_littlefs_file_size(enum infuse_littlefs_folder folder, uint32_t file);

/**
 * @brief Read file metadata
 *
 * @param folder Folder containing the file
 * @param file Filename inside the folder
 * @param metadata Output storage for metadata
 *
 * @retval 0 on success
 * @retval -EAGAIN filesystem not mounted
 * @retval -EINVAL other file open for reading or writing
 * @retval -ENOENT file does not exist
 * @retval -errno other negative error code on failure
 */
int infuse_littlefs_file_metadata(enum infuse_littlefs_folder folder, uint32_t file,
				  struct infuse_littlefs_metadata *metadata);

/**
 * @brief Callback run by @ref infuse_littlefs_folder_iter
 *
 * @param folder Folder that file exists in
 * @param file File that exists in the folder
 *
 * @retval true to continue iterating
 * @retval false to terminate iteration
 */
typedef bool (*infuse_littlefs_file_cb)(enum infuse_littlefs_folder folder, uint32_t file,
					void *user_data);

/**
 * @brief Iterate all files inside a folder
 *
 * @param folder Folder to iterate over
 * @param cb Callback to run on each file found
 * @param user_data Arbitrary pointer to provide to @a cb
 *
 * @retval 0 on success
 * @retval -EAGAIN filesystem not mounted
 * @retval -EINVAL other file open for reading or writing
 * @retval -errno other negative error code on failure
 */
int infuse_littlefs_folder_iter(enum infuse_littlefs_folder folder, infuse_littlefs_file_cb cb,
				void *user_data);

/**
 * @brief Open a file on the filesystem
 *
 * @param folder Folder to create file in
 * @param file Filename inside the folder
 *
 * @retval 0 on success
 * @retval -EAGAIN filesystem not mounted
 * @retval -ENOENT If file does not exist
 * @retval -EINVAL If previously open file is not yet closed
 * @retval -errno Other error code on failure
 */
int infuse_littlefs_file_open(enum infuse_littlefs_folder folder, uint32_t file);

/**
 * @brief Read data from file opened by @ref infuse_littlefs_file_open
 *
 * @param data Buffer for read data
 * @param data_len Length of @a data in bytes
 *
 * @retval >0 Number of bytes read
 * @retval 0 No more data remaining in file
 * @retval -EINVAL If file has not been opened with @ref infuse_littlefs_file_open
 * @retval -errno Other error code on failure
 */
int infuse_littlefs_file_read(void *data, size_t data_len);

/**
 * @brief Set current read offset of file opened by @ref infuse_littlefs_file_open
 *
 * @param offset New read offset in bytes
 *
 * @retval >= 0 Updated file offset in bytes
 * @retval -EINVAL If file has not been opened with @ref infuse_littlefs_file_open
 * @retval -errno Other error code on failure
 */
int infuse_littlefs_file_seek(uint32_t offset);

/**
 * @brief Create a new file in the filesystem
 *
 * Parameter @a metadata *MUST* remain accessible until the final call to
 * @ref infuse_littlefs_file_close. As the attribute structure is not committed
 * until the file is closed, the values can be updated at runtime at any point
 * until the @ref infuse_littlefs_file_close call.
 *
 * @param folder Folder to create file in
 * @param file Filename
 * @param metadata Infuse metadata object to associate with file
 *
 * @retval 0 on success
 * @retval -EAGAIN filesystem not mounted
 * @retval -EEXIST If file already exists
 * @retval -EINVAL If previously open file is not yet closed
 * @retval -errno Other error code on failure
 */
int infuse_littlefs_file_create(enum infuse_littlefs_folder folder, uint32_t file,
				struct infuse_littlefs_metadata *metadata);

/**
 * @brief Write data to file opened by @ref infuse_littlefs_file_create
 *
 * @param data Data to write
 * @param data_len Length of @a data in bytes
 *
 * @retval >0 Number of bytes written
 * @retval -EINVAL If file has not been opened with @ref infuse_littlefs_file_create
 * @retval -errno Other error code on failure
 */
int infuse_littlefs_file_write(const void *data, size_t data_len);

/**
 * @brief Close currently open file
 *
 * @retval 0 On success
 * @retval -EINVAL If file has not been opened with @ref infuse_littlefs_file_create
 *                 or @ref infuse_littlefs_file_open
 * @retval -errno Other error code on failure
 */
int infuse_littlefs_file_close(void);

/**
 * @brief Delete a file on the filesystem
 *
 * @param folder Folder to create file in
 * @param file Filename inside the folder
 *
 * @retval 0 on success
 * @retval -EAGAIN filesystem not mounted
 * @retval -ENOENT If file does not exist
 * @retval -EINVAL If previously open file is not yet closed
 * @retval -errno Other error code on failure
 */
int infuse_littlefs_file_delete(enum infuse_littlefs_folder folder, uint32_t file);

/**
 * @brief Calculate the CRC32-IEEE over a file
 *
 * @note Automatically opens and closes underlying file
 *
 * @note Depends on @kconfig{CONFIG_CRC}
 *
 * @param folder Folder containing the file
 * @param file Filename to compute CRC of
 * @param max_len Maximum length to compute CRC over
 * @param crc32_ieee Output storage for CRC32-IEEE result
 * @param working_mem Working memory buffer to read data into
 * @param working_mem_len Length of working memory buffer
 *
 * @retval 0 on success
 * @retval -EAGAIN filesystem not mounted
 * @retval -ENOENT If file does not exist
 * @retval -EINVAL If previously open file is not yet closed
 * @retval -errno Other error code on failure
 */
int infuse_littlefs_file_crc32(enum infuse_littlefs_folder folder, uint32_t file, uint32_t max_len,
			       uint32_t *crc32_ieee, void *working_mem, size_t working_mem_len);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_FS_LITTLEFS_H_ */
