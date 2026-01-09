/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/drivers/flash/flash_simulator.h>

#include <infuse/types.h>
#include <infuse/rpc/types.h>
#include <infuse/security.h>
#include <infuse/fs/kv_store.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/data_logger/logger.h>

int logger_flash_map_init(const struct device *dev);

static size_t flash_buffer_size;
static uint8_t *flash_buffer;
uint8_t data_block[512];

static void send_data_logger_state_command(uint32_t request_id, uint8_t logger, uint16_t rpc_id)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_data_logger_state_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = rpc_id,
			},
		.logger = logger,
	};

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static void send_data_logger_read_command(uint32_t request_id, uint8_t logger, uint32_t start,
					  uint32_t end)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_data_logger_read_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_DATA_LOGGER_READ,
			},
		.data_header =
			{
				.size = (end - start + 1) * 512,
				.rx_ack_period = 0,
			},
		.logger = logger,
		.start_block = start,
		.last_block = end,
	};

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static void send_data_logger_read_available_command(uint32_t request_id, uint8_t logger,
						    uint32_t start, uint32_t num)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_data_logger_read_available_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_DATA_LOGGER_READ_AVAILABLE,
			},
		.data_header =
			{
				.size = num * 512,
				.rx_ack_period = 0,
			},
		.logger = logger,
		.start_block = start,
		.num_blocks = num,
	};

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static void send_data_logger_read_chunks_command(uint32_t request_id, uint8_t logger,
						 uint8_t num_chunks,
						 const struct rpc_struct_data_logger_chunk *chunks)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	uint32_t total_bytes = 0;

	for (int i = 0; i < num_chunks; i++) {
		total_bytes += chunks[i].num_bytes;
	}

	struct rpc_data_logger_read_chunks_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_DATA_LOGGER_READ_CHUNKS,
			},
		.data_header =
			{
				.size = total_bytes,
				.rx_ack_period = 0,
			},
		.logger = logger,
		.num_chunks = num_chunks};

	/* Push command at RPC server */
	epacket_dummy_receive_extra(epacket_dummy, &header, &params, sizeof(params), chunks,
				    num_chunks * sizeof(*chunks));
}

static void send_data_logger_erase_command(uint32_t request_id, uint8_t logger, bool erase_empty)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_data_logger_erase_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_DATA_LOGGER_ERASE,
			},
		.logger = logger,
		.erase_empty = erase_empty,
	};

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static struct net_buf *expect_rpc_response(uint32_t request_id, uint16_t command_id, int rc)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct infuse_rpc_rsp_header *response;
	struct net_buf *rsp;

	zassert_not_null(response_queue);

	/* Response was sent */
	rsp = k_fifo_get(response_queue, K_MSEC(100));
	zassert_not_null(rsp);
	net_buf_pull_mem(rsp, sizeof(struct epacket_dummy_frame));
	response = (void *)rsp->data;

	/* Parameters match what we expect */
	zassert_equal(request_id, response->request_id);
	zassert_equal(command_id, response->command_id);
	zassert_equal(rc, response->return_code);

	/* Return the response */
	return rsp;
}

static void basic_tests(const struct device *flash_logger, uint16_t rpc_id)
{
	struct net_buf *rsp;

	/* Data logger that doesn't exist */
	send_data_logger_state_command(10, RPC_ENUM_DATA_LOGGER_FLASH_REMOVABLE, rpc_id);
	rsp = expect_rpc_response(10, rpc_id, -ENODEV);
	net_buf_unref(rsp);

	/* Data logger that failed to init */
	flash_logger->state->init_res += 1;
	send_data_logger_state_command(11, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD, rpc_id);
	rsp = expect_rpc_response(11, rpc_id, -EBADF);
	net_buf_unref(rsp);
	flash_logger->state->init_res -= 1;

	/* Data logger that exists */
	send_data_logger_state_command(10, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD, rpc_id);
	rsp = expect_rpc_response(10, rpc_id, 0);

	if (rpc_id == RPC_ID_DATA_LOGGER_STATE) {
		struct rpc_data_logger_state_response *response = (void *)rsp->data;

		zassert_equal(512, response->block_size);
		zassert_equal(0, response->bytes_logged);
		zassert_equal(0, response->boot_block);
		zassert_equal(0, response->earliest_block);
		zassert_equal(0, response->current_block);
	} else {
		struct rpc_data_logger_state_v2_response *response = (void *)rsp->data;

		zassert_equal(512, response->block_size);
		zassert_equal(0, response->bytes_logged);
		zassert_equal(0, response->boot_block);
		zassert_equal(0, response->earliest_block);
		zassert_equal(0, response->current_block);
	}
	net_buf_unref(rsp);
}

ZTEST(rpc_command_data_logger, test_data_logger_state)
{
	const struct device *flash_logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_flash));
	struct rpc_data_logger_state_response *response;
	struct net_buf *rsp;
	uint64_t logged = 0;
	uint32_t earliest;
	int rc;

	zassert_true(device_is_ready(flash_logger));

	basic_tests(flash_logger, RPC_ID_DATA_LOGGER_STATE);
	basic_tests(flash_logger, RPC_ID_DATA_LOGGER_STATE_V2);

	/* Give uptime a chance to be not 0 */
	k_sleep(K_MSEC(1500));

	for (int i = 0; i < 32; i++) {
		sys_rand_get(data_block, sizeof(data_block));
		rc = data_logger_block_write(flash_logger, INFUSE_TDF, data_block,
					     sizeof(data_block));
		zassert_equal(0, rc);
		logged += sizeof(data_block);

		send_data_logger_state_command(10, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD,
					       RPC_ID_DATA_LOGGER_STATE);
		rsp = expect_rpc_response(10, RPC_ID_DATA_LOGGER_STATE, 0);
		response = (void *)rsp->data;
		zassert_equal(0, response->boot_block);
		zassert_equal(logged, response->bytes_logged);
		zassert_equal(i + 1, response->current_block);
		zassert_equal(k_uptime_seconds(), response->uptime);
		earliest = response->earliest_block;
		net_buf_unref(rsp);
	}
	zassert_equal(32 - 8, earliest);
}

ZTEST(rpc_command_data_logger, test_data_logger_read_invalid)
{
	const struct device *flash_logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_flash));
	struct net_buf *rsp;
	int rc;

	/* Write 4 blocks */
	for (int i = 0; i < 4; i++) {
		sys_rand_get(data_block, sizeof(data_block));
		rc = data_logger_block_write(flash_logger, INFUSE_TDF, data_block,
					     sizeof(data_block));
		zassert_equal(0, rc);
	}

	/* Non existent device */
	send_data_logger_read_command(15, RPC_ENUM_DATA_LOGGER_FLASH_REMOVABLE, 0, 10);
	rsp = expect_rpc_response(15, RPC_ID_DATA_LOGGER_READ, -ENODEV);
	net_buf_unref(rsp);

	/* Device that failed to init */
	flash_logger->state->init_res += 1;
	send_data_logger_read_command(30, RPC_ENUM_DATA_LOGGER_FLASH_REMOVABLE, 0, 10);
	rsp = expect_rpc_response(30, RPC_ID_DATA_LOGGER_READ, -ENODEV);
	net_buf_unref(rsp);
	flash_logger->state->init_res -= 1;

	/* More data than exists */
	send_data_logger_read_command(16, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD, 0, 10);
	rsp = expect_rpc_response(16, RPC_ID_DATA_LOGGER_READ, -EINVAL);
	net_buf_unref(rsp);

	/* End before start */
	send_data_logger_read_command(17, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD, 3, 1);
	rsp = expect_rpc_response(17, RPC_ID_DATA_LOGGER_READ, -EINVAL);
	net_buf_unref(rsp);

	/* Write 16 blocks */
	for (int i = 0; i < 16; i++) {
		sys_rand_get(data_block, sizeof(data_block));
		rc = data_logger_block_write(flash_logger, INFUSE_TDF, data_block,
					     sizeof(data_block));
		zassert_equal(0, rc);
	}

	/* Data that no longer exists on device */
	send_data_logger_read_command(18, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD, 2, 18);
	rsp = expect_rpc_response(18, RPC_ID_DATA_LOGGER_READ, -EINVAL);
	net_buf_unref(rsp);
}

static void run_logger_read(uint16_t epacket_size, uint32_t start, uint32_t end, int dc_after)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct rpc_data_logger_read_response *rsp;
	struct epacket_dummy_frame *tx_header;
	uint32_t request_id = sys_rand32_get();
	bool receiving = true;
	struct net_buf *tx;
	struct infuse_rpc_data *data;
	uint32_t bytes_received = 0;
	uint32_t expected_offset = 0;
	uint32_t crc = 0;
	int packets_received = 0;

	uint32_t actual_end = (end == UINT32_MAX) ? 7 : end;
	uint32_t start_offset = 512 * start;
	uint32_t num = 512 * (actual_end - start + 1);
	uint32_t flash_crc = crc32_ieee(flash_buffer + start_offset, num);

	epacket_dummy_set_max_packet(epacket_size);
	epacket_dummy_set_interface_state(epacket_dummy, true);

	send_data_logger_read_command(request_id, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD, start, end);

	while (receiving) {
		tx = k_fifo_get(tx_fifo, K_MSEC(100));
		zassert_not_null(tx);
		tx_header = net_buf_pull_mem(tx, sizeof(*tx_header));
		zassert_equal(EPACKET_AUTH_DEVICE, tx_header->auth);

		if (tx_header->type == INFUSE_RPC_RSP) {
			receiving = false;
			rsp = net_buf_pull_mem(tx, sizeof(*rsp));
			zassert_equal(request_id, rsp->header.request_id);
			zassert_equal(RPC_ID_DATA_LOGGER_READ, rsp->header.command_id);
			zassert_equal(0, rsp->header.return_code);
			zassert_equal(crc, rsp->sent_crc);
			zassert_equal(flash_crc, rsp->sent_crc);
			zassert_equal(num, rsp->sent_len);
			zassert_equal(bytes_received, rsp->sent_len);
		} else if (tx_header->type == INFUSE_RPC_DATA) {
			data = net_buf_pull_mem(tx, sizeof(*data));
			zassert_true(tx->len > 0);
			zassert_equal(request_id, data->request_id);
			zassert_equal(expected_offset, data->offset);
			crc = crc32_ieee_update(crc, data->payload, tx->len);
			bytes_received += tx->len;
			expected_offset += tx->len;
		} else {
			zassert_true(false, "Unexpected packet type");
		}

		net_buf_unref(tx);

		if (++packets_received == dc_after) {
			epacket_dummy_set_max_packet(0);
			epacket_dummy_set_interface_state(epacket_dummy, false);
			tx = k_fifo_get(tx_fifo, K_MSEC(500));
			zassert_is_null(tx);
			break;
		}
	}
}

ZTEST(rpc_command_data_logger, test_data_logger_read)
{
	const struct device *flash_logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_flash));
	int rc;

	/* Write 8 blocks */
	for (int i = 0; i < 8; i++) {
		sys_rand_get(data_block, sizeof(data_block));
		rc = data_logger_block_write(flash_logger, INFUSE_TDF, data_block,
					     sizeof(data_block));
		zassert_equal(0, rc);
	}

	/* Run various data logger reads */
	run_logger_read(64, 0, 4, 0);
	run_logger_read(63, 0, 6, 0);
	run_logger_read(61, 2, 4, 0);
	run_logger_read(62, 2, UINT32_MAX, 0);
}

ZTEST(rpc_command_data_logger, test_data_logger_read_disconnect)
{
	const struct device *flash_logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_flash));
	int rc;

	/* Write 8 blocks */
	for (int i = 0; i < 8; i++) {
		sys_rand_get(data_block, sizeof(data_block));
		rc = data_logger_block_write(flash_logger, INFUSE_TDF, data_block,
					     sizeof(data_block));
		zassert_equal(0, rc);
	}

	/* Attempt to read but disconnects */
	for (int i = 0; i < 4; i++) {
		run_logger_read(64, 0, 7, 3);
	}
}

static void run_logger_read_available(uint16_t epacket_size, uint32_t start, uint32_t num,
				      int dc_after, uint32_t expected_bytes)
{
	const struct device *flash_logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_flash));
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct rpc_data_logger_read_available_response *rsp;
	struct epacket_dummy_frame *tx_header;
	uint32_t request_id = sys_rand32_get();
	bool receiving = true;
	struct net_buf *tx;
	struct data_logger_state logger_state;
	struct infuse_rpc_data *data;
	uint32_t bytes_received = 0;
	uint32_t expected_offset = 0;
	uint32_t crc = 0;
	int packets_received = 0;

	data_logger_get_state(flash_logger, &logger_state);

	uint32_t actual_start = MAX(start, logger_state.earliest_block);
	uint32_t actual_end = (num == UINT32_MAX) ? 7 : MIN(actual_start + num - 1, 7);
	uint32_t start_offset = 512 * actual_start;
	uint32_t num_bytes = 512 * (actual_end - actual_start + 1);
	uint32_t flash_crc = crc32_ieee(flash_buffer + start_offset, num_bytes);

	zassert_equal(expected_bytes, num_bytes);

	epacket_dummy_set_max_packet(epacket_size);
	epacket_dummy_set_interface_state(epacket_dummy, true);

	send_data_logger_read_available_command(request_id, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD,
						start, num);

	while (receiving) {
		tx = k_fifo_get(tx_fifo, K_MSEC(100));
		zassert_not_null(tx);
		tx_header = net_buf_pull_mem(tx, sizeof(*tx_header));
		zassert_equal(EPACKET_AUTH_DEVICE, tx_header->auth);

		if (tx_header->type == INFUSE_RPC_RSP) {
			receiving = false;
			rsp = net_buf_pull_mem(tx, sizeof(*rsp));
			zassert_equal(request_id, rsp->header.request_id);
			zassert_equal(RPC_ID_DATA_LOGGER_READ_AVAILABLE, rsp->header.command_id);
			zassert_equal(0, rsp->header.return_code);
			zassert_equal(num_bytes, rsp->sent_len);
			zassert_equal(bytes_received, rsp->sent_len);
			zassert_equal(crc, rsp->sent_crc);
			zassert_equal(flash_crc, rsp->sent_crc);
			zassert_equal(512, rsp->block_size);
			zassert_equal(actual_start, rsp->start_block_actual);
			zassert_equal(logger_state.current_block, rsp->current_block);

		} else if (tx_header->type == INFUSE_RPC_DATA) {
			data = net_buf_pull_mem(tx, sizeof(*data));
			zassert_true(tx->len > 0);
			zassert_equal(request_id, data->request_id);
			zassert_equal(expected_offset, data->offset);
			crc = crc32_ieee_update(crc, data->payload, tx->len);
			bytes_received += tx->len;
			expected_offset += tx->len;
		} else {
			zassert_true(false, "Unexpected packet type");
		}

		net_buf_unref(tx);

		if (++packets_received == dc_after) {
			epacket_dummy_set_max_packet(0);
			epacket_dummy_set_interface_state(epacket_dummy, false);
			tx = k_fifo_get(tx_fifo, K_MSEC(500));
			zassert_is_null(tx);
			break;
		}
	}
}

ZTEST(rpc_command_data_logger, test_data_logger_read_available)
{
	const struct device *flash_logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_flash));
	int rc;

	/* Write 8 blocks */
	for (int i = 0; i < 8; i++) {
		sys_rand_get(data_block, sizeof(data_block));
		rc = data_logger_block_write(flash_logger, INFUSE_TDF, data_block,
					     sizeof(data_block));
		zassert_equal(0, rc);
	}

	/* Run various data logger reads */
	run_logger_read_available(64, 0, 4, 0, 2048);
	run_logger_read_available(63, 0, 6, 0, 3072);
	run_logger_read_available(61, 2, 2, 0, 1024);
	run_logger_read_available(62, 2, UINT32_MAX, 0, 3072);

	/* Write 2 more blocks, which will result in erases */
	for (int i = 0; i < 2; i++) {
		sys_rand_get(data_block, sizeof(data_block));
		rc = data_logger_block_write(flash_logger, INFUSE_TDF, data_block,
					     sizeof(data_block));
		zassert_equal(0, rc);
	}

	/* Request reads from 0 but that block doesn't exist */
	run_logger_read_available(64, 0, 2, 0, 1024);
	run_logger_read_available(63, 0, 4, 0, 2048);
	/* Don't try UINT32_MAX since our flash CRC validation doesn't handle wrapping */
	run_logger_read_available(61, 0, 6, 0, 3072);
}

static void run_logger_read_chunks(uint16_t epacket_size, uint8_t num_chunks,
				   const struct rpc_struct_data_logger_chunk *chunks,
				   int expected_result, uint32_t expected_bytes)
{
	const struct device *flash_logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_flash));
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct rpc_data_logger_read_chunks_response *rsp;
	struct epacket_dummy_frame *tx_header;
	uint32_t request_id = sys_rand32_get();
	bool receiving = true;
	struct net_buf *tx;
	struct data_logger_state logger_state;
	struct infuse_rpc_data *data;
	uint32_t bytes_received = 0;
	uint32_t expected_offset = 0;
	uint32_t start_offset;
	uint32_t flash_crc = 0;
	uint32_t crc = 0;

	data_logger_get_state(flash_logger, &logger_state);

	for (int i = 0; i < num_chunks; i++) {
		start_offset =
			chunks[i].start_block * logger_state.block_size + chunks[i].start_offset;
		flash_crc = crc32_ieee_update(flash_crc, flash_buffer + start_offset,
					      chunks[i].num_bytes);
	}

	epacket_dummy_set_max_packet(epacket_size);
	epacket_dummy_set_interface_state(epacket_dummy, true);

	send_data_logger_read_chunks_command(request_id, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD,
					     num_chunks, chunks);

	while (receiving) {
		tx = k_fifo_get(tx_fifo, K_MSEC(100));
		zassert_not_null(tx);
		tx_header = net_buf_pull_mem(tx, sizeof(*tx_header));
		zassert_equal(EPACKET_AUTH_DEVICE, tx_header->auth);

		if (tx_header->type == INFUSE_RPC_RSP) {
			receiving = false;
			rsp = net_buf_pull_mem(tx, sizeof(*rsp));
			zassert_equal(request_id, rsp->header.request_id);
			zassert_equal(RPC_ID_DATA_LOGGER_READ_CHUNKS, rsp->header.command_id);
			zassert_equal(expected_result, rsp->header.return_code);
			zassert_equal(expected_bytes, rsp->sent_len);
			zassert_equal(bytes_received, rsp->sent_len);
			if (expected_result == 0) {
				zassert_equal(crc, rsp->sent_crc);
				zassert_equal(flash_crc, rsp->sent_crc);
			}
			zassert_equal(512, rsp->block_size);
			zassert_equal(logger_state.current_block, rsp->current_block);

		} else if (tx_header->type == INFUSE_RPC_DATA) {
			data = net_buf_pull_mem(tx, sizeof(*data));
			zassert_true(tx->len > 0);
			zassert_equal(request_id, data->request_id);
			zassert_equal(expected_offset, data->offset);
			crc = crc32_ieee_update(crc, data->payload, tx->len);
			bytes_received += tx->len;
			expected_offset += tx->len;
		} else {
			zassert_true(false, "Unexpected packet type");
		}

		net_buf_unref(tx);
	}
}

ZTEST(rpc_command_data_logger, test_data_logger_read_chunks)
{
	const struct device *flash_logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_flash));
	const struct rpc_struct_data_logger_chunk chunks[] = {
		{.start_block = 0, .start_offset = 0, .num_bytes = 600},
		{.start_block = 2, .start_offset = 500, .num_bytes = 200},
		{.start_block = 3, .start_offset = 20, .num_bytes = 2005},
		{.start_block = 5, .start_offset = 0, .num_bytes = 512},
		{.start_block = 0, .start_offset = 0, .num_bytes = 600},
	};
	int rc;

	/* Write 8 blocks */
	for (int i = 0; i < 8; i++) {
		sys_rand_get(data_block, sizeof(data_block));
		rc = data_logger_block_write(flash_logger, INFUSE_TDF, data_block,
					     sizeof(data_block));
		zassert_equal(0, rc);
	}

	/* Run various chunked data logger reads */
	run_logger_read_chunks(64, 1, chunks + 0, 0, 600);
	run_logger_read_chunks(63, 2, chunks + 0, 0, 800);
	run_logger_read_chunks(62, 2, chunks + 1, 0, 2205);
	run_logger_read_chunks(61, 3, chunks + 0, 0, 2805);
	run_logger_read_chunks(62, 2, chunks + 2, 0, 2517);
	run_logger_read_chunks(63, 1, chunks + 3, 0, 512);

	/* Write 2 more blocks, which will result in erases */
	for (int i = 0; i < 2; i++) {
		sys_rand_get(data_block, sizeof(data_block));
		rc = data_logger_block_write(flash_logger, INFUSE_TDF, data_block,
					     sizeof(data_block));
		zassert_equal(0, rc);
	}

	/* Request reads from 0 but that block doesn't exist */
	run_logger_read_chunks(64, 1, chunks + 0, -ENOENT, 0);

	/* Request reads from 0 as the second chunk but that block doesn't exist */
	run_logger_read_chunks(62, 2, chunks + 3, -ENOENT, 512);
}

ZTEST(rpc_command_data_logger, test_data_logger_erase_invalid)
{
	const struct device *flash_logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_flash));
	struct net_buf *rsp;

	send_data_logger_erase_command(0x1234, UINT8_MAX, false);
	rsp = expect_rpc_response(0x1234, RPC_ID_DATA_LOGGER_ERASE, -ENODEV);
	net_buf_unref(rsp);

	/* Pretend logger failed to initialise */
	flash_logger->state->init_res += 1;
	/* Try to erase */
	send_data_logger_erase_command(0x1234, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD, false);
	rsp = expect_rpc_response(0x1234, RPC_ID_DATA_LOGGER_ERASE, -EBADF);
	net_buf_unref(rsp);
	/* Restore init result */
	flash_logger->state->init_res -= 1;
}

ZTEST(rpc_command_data_logger, test_data_logger_erase)
{
	const struct device *flash_logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_flash));
	struct data_logger_state state;
	struct net_buf *rsp;
	int rc;

	/* Write 8 blocks */
	for (int i = 0; i < 8; i++) {
		sys_rand_get(data_block, sizeof(data_block));
		rc = data_logger_block_write(flash_logger, INFUSE_TDF, data_block,
					     sizeof(data_block));
		zassert_equal(0, rc);
	}

	/* Initial state */
	data_logger_get_state(flash_logger, &state);
	zassert_equal(0, state.boot_block);
	zassert_equal(8, state.current_block);
	zassert_equal(8 * 512, state.bytes_logged);

	/* Erase request */
	send_data_logger_erase_command(0x1235, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD, true);
	rsp = expect_rpc_response(0x1235, RPC_ID_DATA_LOGGER_ERASE, 0);
	net_buf_unref(rsp);

	/* Block statistics are reset, bytes logged are not.
	 * This allows logging statistics to continue working despite the reset.
	 */
	data_logger_get_state(flash_logger, &state);
	zassert_equal(0, state.boot_block);
	zassert_equal(0, state.current_block);
	zassert_equal(8 * 512, state.bytes_logged);

	/* Write some more blocks */
	for (int i = 0; i < 5; i++) {
		sys_rand_get(data_block, sizeof(data_block));
		rc = data_logger_block_write(flash_logger, INFUSE_TDF, data_block,
					     sizeof(data_block));
		zassert_equal(0, rc);
	}
	data_logger_get_state(flash_logger, &state);
	zassert_equal(0, state.boot_block);
	zassert_equal(5, state.current_block);
	zassert_equal(13 * 512, state.bytes_logged);
}

void data_logger_reset(void *fixture)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	const struct device *data_logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_flash));

	epacket_dummy_set_max_packet(CONFIG_EPACKET_PACKET_SIZE_MAX);
	epacket_dummy_set_interface_state(epacket_dummy, true);

	/* Erase amd reinitialise loggers */
	memset(flash_buffer, 0xFF, flash_buffer_size);
	logger_flash_map_init(data_logger);
}

static bool test_data_init(const void *global_state)
{
	flash_buffer = flash_simulator_get_memory(DEVICE_DT_GET(DT_NODELABEL(sim_flash)),
						  &flash_buffer_size);
	return true;
}

ZTEST_SUITE(rpc_command_data_logger, test_data_init, NULL, data_logger_reset, NULL, NULL);
