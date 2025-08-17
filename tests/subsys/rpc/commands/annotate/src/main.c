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
#include <infuse/tdf/tdf.h>
#include <infuse/tdf/definitions.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>

int logger_flash_map_init(const struct device *dev);

static size_t flash_buffer_size;
static uint8_t *flash_buffer;
uint8_t data_block[512];

static void send_annotate_command(uint32_t request_id, uint8_t logger, uint32_t t, char *event)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct rpc_annotate_request params = {
		.header =
			{
				.request_id = request_id,
				.command_id = RPC_ID_ANNOTATE,
			},
		.logger = logger,
		.timestamp = t,
	};

	/* Push command at RPC server */
	epacket_dummy_receive_extra(epacket_dummy, &header, &params, sizeof(params), event,
				    strlen(event));
}

static struct net_buf *expect_rpc_response(uint32_t request_id, int rc)
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
	zassert_equal(RPC_ID_ANNOTATE, response->command_id);
	zassert_equal(rc, response->return_code);

	/* Return the response */
	return rsp;
}

ZTEST(rpc_command_annotate, test_annotate)
{
	const struct device *flash_logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_flash));
	const struct device *flash_tdf_logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_flash));
	struct rpc_annotate_response *response;
	struct tdf_buffer_state parse_state;
	struct data_logger_state state;
	struct tdf_annotation *tdf;
	struct tdf_parsed parsed;
	struct net_buf *rsp;
	int rc;

	zassert_true(device_is_ready(flash_tdf_logger));

	/* TDF logger that doesn't exist */
	send_annotate_command(10, RPC_ENUM_DATA_LOGGER_FLASH_REMOVABLE, 0, "X");
	rsp = expect_rpc_response(10, -ENODEV);
	net_buf_unref(rsp);

	/* TDF logger that failed to init */
	flash_tdf_logger->state->init_res += 1;
	send_annotate_command(11, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD, 0, "X");
	rsp = expect_rpc_response(11, -EBADF);
	net_buf_unref(rsp);
	flash_tdf_logger->state->init_res -= 1;

	/* No event string */
	send_annotate_command(12, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD, 0, "");
	rsp = expect_rpc_response(12, -EINVAL);
	response = (void *)rsp->data;
	net_buf_unref(rsp);

	/* Proper annotation */
	send_annotate_command(13, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD, 100, "EVENT");
	rsp = expect_rpc_response(13, 0);
	response = (void *)rsp->data;
	net_buf_unref(rsp);

	/* Flush to disk and expect TDF to have been logged */
	tdf_data_logger_flush(TDF_DATA_LOGGER_FLASH);
	data_logger_get_state(flash_logger, &state);
	zassert_equal(1, state.current_block);

	/* Read it back out and validate the data */
	rc = data_logger_block_read(flash_logger, 0, 0, data_block, sizeof(data_block));
	zassert_equal(0, rc);
	zassert_equal(INFUSE_TDF, data_block[1]);
	tdf_parse_start(&parse_state, data_block + 2, sizeof(data_block) - 2);

	rc = tdf_parse(&parse_state, &parsed);
	zassert_equal(0, rc);
	zassert_equal(TDF_ANNOTATION, parsed.tdf_id);
	tdf = parsed.data;
	zassert_equal(100, tdf->timestamp);
	zassert_mem_equal("EVENT", tdf->event, strlen("EVENT"));

	/* No other TDFs */
	rc = tdf_parse(&parse_state, &parsed);
	zassert_not_equal(0, rc);
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

ZTEST_SUITE(rpc_command_annotate, test_data_init, NULL, data_logger_reset, NULL, NULL);
