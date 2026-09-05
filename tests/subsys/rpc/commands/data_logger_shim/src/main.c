/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>

#include <infuse/data_logger/backend/shim.h>
#include <infuse/data_logger/logger.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/epacket/packet.h>
#include <infuse/rpc/types.h>

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

	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static void send_data_logger_erase_command(uint32_t request_id, uint8_t logger, uint8_t erase_mode)
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
		.erase_empty = erase_mode,
	};

	epacket_dummy_receive(epacket_dummy, &header, &params, sizeof(params));
}

static struct net_buf *expect_rpc_response(uint32_t request_id, uint16_t command_id, int rc)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct infuse_rpc_rsp_header *response;
	struct net_buf *rsp;

	zassert_not_null(response_queue);

	rsp = k_fifo_get(response_queue, K_MSEC(100));
	zassert_not_null(rsp);
	net_buf_pull_mem(rsp, sizeof(struct epacket_dummy_frame));
	response = (void *)rsp->data;

	zassert_equal(request_id, response->request_id);
	zassert_equal(command_id, response->command_id);
	zassert_equal(rc, response->return_code);

	return rsp;
}

ZTEST(rpc_command_data_logger_shim, test_data_logger_state_shim)
{
	const struct device *logger = DEVICE_DT_GET_ONE(embeint_data_logger_shim);
	struct rpc_data_logger_state_response *rsp_v1;
	struct rpc_data_logger_state_v2_response *rsp_v2;
	struct net_buf *rsp;

	zassert_true(device_is_ready(logger));

	send_data_logger_state_command(0x1000, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD,
				       RPC_ID_DATA_LOGGER_STATE);
	rsp = expect_rpc_response(0x1000, RPC_ID_DATA_LOGGER_STATE, 0);
	rsp_v1 = (void *)rsp->data;
	zassert_equal(8, rsp_v1->physical_blocks);
	zassert_equal(16, rsp_v1->logical_blocks);
	zassert_equal(512, rsp_v1->block_size);
	net_buf_unref(rsp);

	logger_shim_set_current_block(logger, 3);
	send_data_logger_state_command(0x1001, RPC_ENUM_DATA_LOGGER_FLASH_REMOVABLE,
				       RPC_ID_DATA_LOGGER_STATE_V2);
	rsp = expect_rpc_response(0x1001, RPC_ID_DATA_LOGGER_STATE_V2, 0);
	rsp_v2 = (void *)rsp->data;
	zassert_equal(3, rsp_v2->current_block);
	zassert_equal(512, rsp_v2->block_size);
	net_buf_unref(rsp);
}

ZTEST(rpc_command_data_logger_shim, test_data_logger_erase_shim)
{
	const struct device *logger = DEVICE_DT_GET_ONE(embeint_data_logger_shim);
	struct data_logger_shim_function_data *shim_data;
	struct data_logger_state state;
	struct net_buf *rsp;

	zassert_true(device_is_ready(logger));
	shim_data = data_logger_backend_shim_data_pointer(logger);

	logger_shim_set_current_block(logger, 5);
	send_data_logger_erase_command(0x2000, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD, 0x00);
	rsp = expect_rpc_response(0x2000, RPC_ID_DATA_LOGGER_ERASE, 0);
	net_buf_unref(rsp);
	zassert_equal(1, shim_data->reset.num_calls);
	zassert_equal(5, shim_data->reset.block_hint);
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.current_block);

	logger_shim_set_current_block(logger, 11);
	send_data_logger_erase_command(0x2001, RPC_ENUM_DATA_LOGGER_FLASH_REMOVABLE, 0x01);
	rsp = expect_rpc_response(0x2001, RPC_ID_DATA_LOGGER_ERASE, 0);
	net_buf_unref(rsp);
	zassert_equal(2, shim_data->reset.num_calls);
	zassert_equal(8, shim_data->reset.block_hint);
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.current_block);
}

ZTEST_SUITE(rpc_command_data_logger_shim, NULL, NULL, NULL, NULL, NULL);
