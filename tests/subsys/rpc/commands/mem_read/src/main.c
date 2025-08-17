/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

#include <infuse/types.h>
#include <infuse/rpc/types.h>
#include <infuse/security.h>
#include <infuse/fs/kv_store.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>

static void send_mem_read_command(uint32_t request_id, void *memory, uint32_t num_bytes)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_mem_read_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_MEM_READ,
			},
		.data_header =
			{
				.size = num_bytes,
				.rx_ack_period = 0,
			},
		.address = (uintptr_t)memory,
	};

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static void run_logger_read(uint16_t epacket_size, uint8_t *memory, uint32_t num_bytes,
			    int dc_after)
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
	uint32_t memory_crc = crc32_ieee(memory, num_bytes);

	epacket_dummy_set_max_packet(epacket_size);
	epacket_dummy_set_interface_state(epacket_dummy, true);

	send_mem_read_command(request_id, memory, num_bytes);

	while (receiving) {
		tx = k_fifo_get(tx_fifo, K_MSEC(100));
		zassert_not_null(tx);
		tx_header = net_buf_pull_mem(tx, sizeof(*tx_header));
		zassert_equal(EPACKET_AUTH_DEVICE, tx_header->auth);

		if (tx_header->type == INFUSE_RPC_RSP) {
			receiving = false;
			rsp = net_buf_pull_mem(tx, sizeof(*rsp));
			zassert_equal(request_id, rsp->header.request_id);
			zassert_equal(RPC_ID_MEM_READ, rsp->header.command_id);
			zassert_equal(0, rsp->header.return_code);
			zassert_equal(crc, rsp->sent_crc);
			zassert_equal(memory_crc, rsp->sent_crc);
			zassert_equal(bytes_received, rsp->sent_len);
		} else if (tx_header->type == INFUSE_RPC_DATA) {
			data = net_buf_pull_mem(tx, sizeof(*data));
			zassert_true(tx->len > 0);
			zassert_equal(request_id, data->request_id);
			zassert_equal(expected_offset, data->offset);
			zassert_mem_equal(memory + expected_offset, data->payload, tx->len);
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

static const uint8_t flash_buffer[128] = {
	[3] = 0x12,
	[63] = 0x34,
	[127] = 0x78,
};

ZTEST(rpc_command_mem_read, test_mem_read)
{
	uint8_t ram_buffer[256];
	uint32_t small_read = 20;

	sys_rand_get(ram_buffer, sizeof(ram_buffer));

	/* Run various memory reads */
	run_logger_read(64, (void *)&small_read, sizeof(small_read), 0);
	run_logger_read(62, (void *)&small_read, 3, 0);
	run_logger_read(63, (void *)ram_buffer, sizeof(ram_buffer), 0);
	run_logger_read(61, (void *)flash_buffer, sizeof(flash_buffer), 0);
}

ZTEST(rpc_command_mem_read, test_mem_read_disconnect)
{
	uint8_t ram_buffer[256];

	sys_rand_get(ram_buffer, sizeof(ram_buffer));

	/* Attempt to read but disconnects */
	for (int i = 0; i < 4; i++) {
		run_logger_read(61, (void *)ram_buffer, sizeof(ram_buffer), 3);
	}
}

ZTEST_SUITE(rpc_command_mem_read, NULL, NULL, NULL, NULL, NULL);
