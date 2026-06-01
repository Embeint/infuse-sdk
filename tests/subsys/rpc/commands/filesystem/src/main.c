/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/flash/flash_simulator.h>

#include <infuse/types.h>
#include <infuse/rpc/types.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/fs/littlefs.h>

static uint8_t *flash_buffer;
static size_t flash_buffer_size;
static uint8_t test_file_contents[64];

static void send_filesystem_ls_command(uint32_t request_id, uint8_t folder, uint8_t skip)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_filesystem_ls_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_FILESYSTEM_LS,
			},
		.folder = folder,
		.skip = skip,
	};

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static struct net_buf *expect_filesystem_ls_response(uint32_t request_id, int16_t rc,
						     uint8_t expected_total,
						     uint8_t expected_present)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_filesystem_ls_response *response;
	struct net_buf *rsp;
	uint8_t actual_responses;

	zassert_not_null(response_queue);

	/* Response was sent */
	rsp = k_fifo_get(response_queue, K_MSEC(1000));
	zassert_not_null(rsp);
	net_buf_pull_mem(rsp, sizeof(struct epacket_dummy_frame));
	response = (void *)rsp->data;

	/* Parameters match what we expect */
	zassert_equal(request_id, response->header.request_id);
	zassert_equal(rc, response->header.return_code);
	zassert_equal(expected_total, response->total_files);
	zassert_equal(expected_present, response->contained_files);
	actual_responses =
		(rsp->len - sizeof(struct rpc_filesystem_ls_response)) / sizeof(*response->files);
	zassert_equal(expected_present, actual_responses);

	/* Return the response */
	return rsp;
}

static void send_filesystem_rm_command(uint32_t request_id, uint8_t folder, uint32_t file)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_filesystem_rm_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_FILESYSTEM_RM,
			},
		.folder = folder,
		.file = file,
	};

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static void expect_filesystem_rm_response(uint32_t request_id, int16_t rc)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_filesystem_ls_response *response;
	struct net_buf *rsp;

	zassert_not_null(response_queue);

	/* Response was sent */
	rsp = k_fifo_get(response_queue, K_MSEC(1000));
	zassert_not_null(rsp);
	net_buf_pull_mem(rsp, sizeof(struct epacket_dummy_frame));
	response = (void *)rsp->data;

	/* Parameters match what we expect */
	zassert_equal(request_id, response->header.request_id);
	zassert_equal(rc, response->header.return_code);

	/* Free the response */
	net_buf_unref(rsp);
}

ZTEST(rpc_command_filesystem, test_filesystem_ls)
{
	struct infuse_littlefs_metadata meta = {
		.timestamp = 0xFF00,
		.identifier = 0xFFAA,
		.crc = 0xFF55,
	};
	struct rpc_filesystem_ls_response *response;
	struct net_buf *rsp;
	uint32_t file;
	int rc;

	/* Init filesystem */
	rc = infuse_littlefs_init();
	zassert_equal(0, rc);

	/* Folder doesn't exist yet */
	send_filesystem_ls_command(0x1000, INFUSE_LFS_FOLDER_ALGORITHMS, 0);
	rsp = expect_filesystem_ls_response(0x1000, -ENOENT, 0, 0);
	net_buf_unref(rsp);

	/* Create a file, empty */
	rc = infuse_littlefs_file_create(INFUSE_LFS_FOLDER_ALGORITHMS, 10, &meta);
	zassert_equal(0, rc);
	rc = infuse_littlefs_file_close();
	zassert_equal(0, rc);

	/* Folder doesn't exist yet */
	send_filesystem_ls_command(0x1001, INFUSE_LFS_FOLDER_ALGORITHMS, 0);
	rsp = expect_filesystem_ls_response(0x1001, 0, 1, 1);
	response = (void *)rsp->data;
	zassert_equal(10, response->files[0].name);
	zassert_equal(0, response->files[0].size);
	zassert_equal(0xFF00, response->files[0].metadata.timestamp);
	zassert_equal(0xFFAA, response->files[0].metadata.identifier);
	zassert_equal(0xFF55, response->files[0].metadata.crc);
	net_buf_unref(rsp);

	/* Delete the file, folder now exists with nothing in it */
	rc = infuse_littlefs_file_delete(INFUSE_LFS_FOLDER_ALGORITHMS, 10);
	zassert_equal(0, rc);

	send_filesystem_ls_command(0x1002, INFUSE_LFS_FOLDER_ALGORITHMS, 0);
	rsp = expect_filesystem_ls_response(0x1002, 0, 0, 0);
	net_buf_unref(rsp);

	/* Create a number of files in general folder */
	for (int i = 0; i < 4; i++) {
		file = 100 + i;
		meta.identifier = file + 1;
		rc = infuse_littlefs_file_create(INFUSE_LFS_FOLDER_GENERAL, 10 + i, &meta);
		zassert_equal(0, rc);
		rc = infuse_littlefs_file_write(test_file_contents, 16 + i);
		zassert_equal(16 + i, rc);
		rc = infuse_littlefs_file_close();
		zassert_equal(0, rc);
	}

	/* Algorithm folder still works */
	send_filesystem_ls_command(0x1003, INFUSE_LFS_FOLDER_ALGORITHMS, 0);
	rsp = expect_filesystem_ls_response(0x1003, 0, 0, 0);
	net_buf_unref(rsp);

	/* General folder querying */
	send_filesystem_ls_command(0x1004, INFUSE_LFS_FOLDER_GENERAL, 0);
	rsp = expect_filesystem_ls_response(0x1004, 0, 4, 4);
	response = (void *)rsp->data;
	for (int i = 0; i < 4; i++) {
		zassert_equal(10 + i, response->files[i].name);
		zassert_equal(16 + i, response->files[i].size);
		zassert_equal(0xFF00, response->files[i].metadata.timestamp);
		zassert_equal(101 + i, response->files[i].metadata.identifier);
		zassert_equal(0xFF55, response->files[i].metadata.crc);
	}
	net_buf_unref(rsp);

	/* Skip files */
	send_filesystem_ls_command(0x1005, INFUSE_LFS_FOLDER_GENERAL, 1);
	rsp = expect_filesystem_ls_response(0x1005, 0, 4, 3);
	response = (void *)rsp->data;
	for (int i = 1; i < 4; i++) {
		zassert_equal(10 + i, response->files[i - 1].name);
		zassert_equal(16 + i, response->files[i - 1].size);
		zassert_equal(0xFF00, response->files[i - 1].metadata.timestamp);
		zassert_equal(101 + i, response->files[i - 1].metadata.identifier);
		zassert_equal(0xFF55, response->files[i - 1].metadata.crc);
	}
	net_buf_unref(rsp);

	/* Reduce payload size */
	epacket_dummy_set_max_packet(sizeof(struct epacket_dummy_frame) +
				     sizeof(struct rpc_filesystem_ls_response) +
				     2 * sizeof(struct rpc_struct_filesystem_file_info));

	send_filesystem_ls_command(0x1005, INFUSE_LFS_FOLDER_GENERAL, 0);
	rsp = expect_filesystem_ls_response(0x1005, 0, 4, 2);
	response = (void *)rsp->data;
	for (int i = 0; i < 2; i++) {
		zassert_equal(10 + i, response->files[i].name);
		zassert_equal(16 + i, response->files[i].size);
		zassert_equal(0xFF00, response->files[i].metadata.timestamp);
		zassert_equal(101 + i, response->files[i].metadata.identifier);
		zassert_equal(0xFF55, response->files[i].metadata.crc);
	}
	net_buf_unref(rsp);
}

ZTEST(rpc_command_filesystem, test_filesystem_rm)
{
	struct infuse_littlefs_metadata meta = {
		.timestamp = 0xCC00,
		.identifier = 0xCCAA,
		.crc = 0xCC55,
	};
	int rc;

	/* Init filesystem */
	rc = infuse_littlefs_init();
	zassert_equal(0, rc);

	/* File doesn't exist yet */
	send_filesystem_rm_command(0x2000, INFUSE_LFS_FOLDER_A_GNSS, 100);
	expect_filesystem_rm_response(0x2000, -ENOENT);

	/* Create some files */
	rc = infuse_littlefs_file_create(INFUSE_LFS_FOLDER_A_GNSS, 100, &meta);
	zassert_equal(0, rc);
	rc = infuse_littlefs_file_close();
	zassert_equal(0, rc);
	rc = infuse_littlefs_file_create(INFUSE_LFS_FOLDER_A_GNSS, 101, &meta);
	zassert_equal(0, rc);
	rc = infuse_littlefs_file_close();
	zassert_equal(0, rc);

	/* Delete one of them */
	send_filesystem_rm_command(0x2001, INFUSE_LFS_FOLDER_A_GNSS, 100);
	expect_filesystem_rm_response(0x2001, 0);

	/* File actually deleted, other file not touched */
	rc = infuse_littlefs_file_open(INFUSE_LFS_FOLDER_A_GNSS, 100);
	zassert_equal(-ENOENT, rc);
	rc = infuse_littlefs_file_open(INFUSE_LFS_FOLDER_A_GNSS, 101);
	zassert_equal(0, rc);
	rc = infuse_littlefs_file_close();
	zassert_equal(0, rc);
}

static bool test_data_init(const void *global_state)
{
	flash_buffer = flash_simulator_get_memory(DEVICE_DT_GET(DT_NODELABEL(sim_flash)),
						  &flash_buffer_size);
	return true;
}

static void test_before(void *fixture)
{
	epacket_dummy_set_max_packet(CONFIG_EPACKET_PACKET_SIZE_MAX);
	memset(flash_buffer, 0xFF, flash_buffer_size);
	for (int i = 0; i < sizeof(test_file_contents); i++) {
		test_file_contents[i] = (i + 1) & 0xFF;
	}
}

static void test_after(void *fixture)
{
	infuse_littlefs_reset();
}

ZTEST_SUITE(rpc_command_filesystem, test_data_init, NULL, test_before, test_after, NULL);
