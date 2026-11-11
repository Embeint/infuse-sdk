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
#include <infuse/rpc/errors.h>
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

static void send_annotate_command(uint32_t request_id, uint8_t logger, uint32_t t,
				  const char *event)
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

static void send_annotate_v2_command_extra(uint32_t request_id, uint8_t logger, uint8_t type,
					   int64_t timestamp, uint32_t duration,
					   int64_t interval_end, uint32_t label_id,
					   const void *extra, size_t extra_len)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
	};
	struct rpc_annotate_v2_request params = {
		.header = {.request_id = request_id, .command_id = RPC_ID_ANNOTATE_V2},
		.timestamp = timestamp,
		.logger = logger,
		.type = type,
		.duration = duration,
		.interval_end = interval_end,
		.label_id = label_id,
	};

	epacket_dummy_receive_extra(epacket_dummy, &header, &params, sizeof(params), extra,
				    extra_len);
}

static void send_annotate_v2_command(uint32_t request_id, uint8_t logger, uint8_t type,
				     int64_t timestamp, uint32_t duration, int64_t interval_end,
				     uint32_t label_id, const uint16_t *tdf_ids, size_t num_tdf_ids)
{
	send_annotate_v2_command_extra(request_id, logger, type, timestamp, duration, interval_end,
				       label_id, tdf_ids, num_tdf_ids * sizeof(*tdf_ids));
}

static void send_annotation_register_command(uint32_t request_id, uint8_t logger, int64_t timestamp,
					     uint32_t label_id, const char *label)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
	};
	struct rpc_annotation_register_request params = {
		.header = {.request_id = request_id, .command_id = RPC_ID_ANNOTATION_REGISTER},
		.timestamp = timestamp,
		.logger = logger,
		.label_id = label_id,
	};

	epacket_dummy_receive_extra(epacket_dummy, &header, &params, sizeof(params), label,
				    strlen(label));
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
	zassert_equal(sizeof(*response), rsp->len);

	/* Return the response */
	return rsp;
}

static size_t read_logged_tdfs(const struct device *flash_logger, struct tdf_parsed *records,
			       size_t max_records)
{
	struct tdf_buffer_state parse_state;
	size_t count = 0;

	tdf_data_logger_flush(TDF_DATA_LOGGER_FLASH);
	zassert_equal(0,
		      data_logger_block_read(flash_logger, 0, 0, data_block, sizeof(data_block)));
	tdf_parse_start(&parse_state, data_block + 2, sizeof(data_block) - 2);
	while (count < max_records && tdf_parse(&parse_state, &records[count]) == 0) {
		count++;
	}

	return count;
}

ZTEST(rpc_command_annotate, test_annotate)
{
	const struct device *flash_logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_flash));
	const struct device *flash_tdf_logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_flash));
	struct tdf_buffer_state parse_state;
	struct data_logger_state state;
	struct tdf_annotation *tdf;
	struct tdf_parsed parsed;
	struct net_buf *rsp;
	int rc;

	zassert_true(device_is_ready(flash_tdf_logger));

	/* TDF logger that doesn't exist */
	send_annotate_command(10, RPC_ENUM_DATA_LOGGER_FLASH_REMOVABLE, 0, "X");
	rsp = expect_rpc_response(10, RPC_ID_ANNOTATE, INFUSE_RPC_ERROR_DEVICE_NOT_FOUND);
	net_buf_unref(rsp);

	/* TDF logger that failed to init */
	flash_tdf_logger->state->init_res += 1;
	send_annotate_command(11, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD, 0, "X");
	rsp = expect_rpc_response(11, RPC_ID_ANNOTATE, INFUSE_RPC_ERROR_DEVICE_NOT_READY);
	net_buf_unref(rsp);
	flash_tdf_logger->state->init_res -= 1;

	/* No event string */
	send_annotate_command(12, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD, 0, "");
	rsp = expect_rpc_response(12, RPC_ID_ANNOTATE, INFUSE_RPC_ERROR_INVALID_ARGUMENT);
	net_buf_unref(rsp);

	/* Proper annotation */
	send_annotate_command(13, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD, 100, "EVENT");
	rsp = expect_rpc_response(13, RPC_ID_ANNOTATE, 0);
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
	zassert_equal(sizeof(*tdf) + strlen("EVENT"), parsed.tdf_len);
	zassert_equal(100, tdf->timestamp);
	zassert_mem_equal("EVENT", tdf->event, strlen("EVENT"));

	/* No other TDFs */
	rc = tdf_parse(&parse_state, &parsed);
	zassert_not_equal(0, rc);
}

ZTEST(rpc_command_annotate, test_annotate_v2)
{
	const struct device *flash_logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_flash));
	struct tdf_parsed records[6];
	const uint16_t one_id[] = {TDF_BATTERY_STATE};
	const uint16_t many_ids[] = {TDF_BATTERY_STATE, TDF_AMBIENT_TEMPERATURE};
	struct tdf_annotation_v2_label_mapping *mapping;
	struct tdf_annotation_v2_instant *instant;
	struct tdf_annotation_v2_duration *duration;
	struct tdf_annotation_v2_interval *interval;
	struct net_buf *rsp;
	size_t count;

	/* Registering a label and annotating with both registered and unregistered labels. */
	send_annotation_register_command(20, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD, 100, 0x11223344,
					 "registered");
	rsp = expect_rpc_response(20, RPC_ID_ANNOTATION_REGISTER, 0);
	net_buf_unref(rsp);
	send_annotate_v2_command(21, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD,
				 RPC_ENUM_ANNOTATION_TYPE_INSTANT, 101, 0, 0, 0x11223344, NULL, 0);
	rsp = expect_rpc_response(21, RPC_ID_ANNOTATE_V2, 0);
	net_buf_unref(rsp);
	send_annotate_v2_command(22, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD,
				 RPC_ENUM_ANNOTATION_TYPE_INSTANT, 102, 0, 0, 0x55667788, one_id,
				 ARRAY_SIZE(one_id));
	rsp = expect_rpc_response(22, RPC_ID_ANNOTATE_V2, 0);
	net_buf_unref(rsp);
	send_annotate_v2_command(23, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD,
				 RPC_ENUM_ANNOTATION_TYPE_DURATION, 103, 123456, 0, 0x11223344,
				 many_ids, ARRAY_SIZE(many_ids));
	rsp = expect_rpc_response(23, RPC_ID_ANNOTATE_V2, 0);
	net_buf_unref(rsp);
	send_annotate_v2_command(24, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD,
				 RPC_ENUM_ANNOTATION_TYPE_INTERVAL, 104, 0, 987654321, 0x55667788,
				 one_id, ARRAY_SIZE(one_id));
	rsp = expect_rpc_response(24, RPC_ID_ANNOTATE_V2, 0);
	net_buf_unref(rsp);

	count = read_logged_tdfs(flash_logger, records, ARRAY_SIZE(records));
	zassert_equal(5, count);

	zassert_equal(TDF_ANNOTATION_V2_LABEL_MAPPING, records[0].tdf_id);
	zassert_equal(100, records[0].time);
	mapping = records[0].data;
	zassert_equal(sizeof(*mapping) + strlen("registered"), records[0].tdf_len);
	zassert_equal(0x11223344, mapping->label_id);
	zassert_mem_equal("registered", mapping->label, strlen("registered"));

	zassert_equal(TDF_ANNOTATION_V2_INSTANT, records[1].tdf_id);
	zassert_equal(101, records[1].time);
	instant = records[1].data;
	zassert_equal(sizeof(*instant), records[1].tdf_len);
	zassert_equal(0x11223344, instant->label_id);

	zassert_equal(TDF_ANNOTATION_V2_INSTANT, records[2].tdf_id);
	zassert_equal(102, records[2].time);
	instant = records[2].data;
	zassert_equal(sizeof(*instant) + sizeof(one_id), records[2].tdf_len);
	zassert_equal(0x55667788, instant->label_id);
	zassert_equal(TDF_BATTERY_STATE, instant->tdf_ids[0]);

	zassert_equal(TDF_ANNOTATION_V2_DURATION, records[3].tdf_id);
	zassert_equal(103, records[3].time);
	duration = records[3].data;
	zassert_equal(sizeof(*duration) + sizeof(many_ids), records[3].tdf_len);
	zassert_equal(0x11223344, duration->label_id);
	zassert_equal(123456, duration->duration_ticks);
	zassert_equal(TDF_BATTERY_STATE, duration->tdf_ids[0]);
	zassert_equal(TDF_AMBIENT_TEMPERATURE, duration->tdf_ids[1]);

	/* The interval is logged as the fifth record after the four above. */
	zassert_equal(TDF_ANNOTATION_V2_INTERVAL, records[4].tdf_id);
	zassert_equal(104, records[4].time);
	interval = records[4].data;
	zassert_equal(sizeof(*interval) + sizeof(one_id), records[4].tdf_len);
	zassert_equal(0x55667788, interval->label_id);
	zassert_equal(987654321, interval->event_end);
	zassert_equal(TDF_BATTERY_STATE, interval->tdf_ids[0]);
}

ZTEST(rpc_command_annotate, test_annotate_invalid_v2_parameters)
{
	const struct device *flash_tdf_logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_flash));
	struct net_buf *rsp;
	uint8_t odd_byte = 0;

	/* TDF IDs must be complete uint16_t values. */
	send_annotate_v2_command_extra(30, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD,
				       RPC_ENUM_ANNOTATION_TYPE_INSTANT, 0, 0, 0, 1, &odd_byte,
				       sizeof(odd_byte));
	rsp = expect_rpc_response(30, RPC_ID_ANNOTATE_V2, INFUSE_RPC_ERROR_INVALID_ARGUMENT);
	net_buf_unref(rsp);

	/* Unknown annotation modes are rejected. */
	send_annotate_v2_command(31, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD, UINT8_MAX, 0, 0, 0, 1,
				 NULL, 0);
	rsp = expect_rpc_response(31, RPC_ID_ANNOTATE_V2, INFUSE_RPC_ERROR_UNSUPPORTED_REQUEST);
	net_buf_unref(rsp);

	/* Interval end must be non-zero and no earlier than the start. */
	send_annotate_v2_command(32, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD,
				 RPC_ENUM_ANNOTATION_TYPE_INTERVAL, 100, 0, 0, 1, NULL, 0);
	rsp = expect_rpc_response(32, RPC_ID_ANNOTATE_V2, INFUSE_RPC_ERROR_INVALID_ARGUMENT);
	net_buf_unref(rsp);
	send_annotate_v2_command(33, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD,
				 RPC_ENUM_ANNOTATION_TYPE_INTERVAL, 100, 0, 99, 1, NULL, 0);
	rsp = expect_rpc_response(33, RPC_ID_ANNOTATE_V2, INFUSE_RPC_ERROR_INVALID_ARGUMENT);
	net_buf_unref(rsp);

	/* Logger selection and readiness are validated for the new command. */
	send_annotate_v2_command(34, RPC_ENUM_DATA_LOGGER_FLASH_REMOVABLE,
				 RPC_ENUM_ANNOTATION_TYPE_INSTANT, 0, 0, 0, 1, NULL, 0);
	rsp = expect_rpc_response(34, RPC_ID_ANNOTATE_V2, INFUSE_RPC_ERROR_DEVICE_NOT_FOUND);
	net_buf_unref(rsp);
	flash_tdf_logger->state->init_res += 1;
	send_annotate_v2_command(35, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD,
				 RPC_ENUM_ANNOTATION_TYPE_INSTANT, 0, 0, 0, 1, NULL, 0);
	rsp = expect_rpc_response(35, RPC_ID_ANNOTATE_V2, INFUSE_RPC_ERROR_DEVICE_NOT_READY);
	net_buf_unref(rsp);
	flash_tdf_logger->state->init_res -= 1;
}

ZTEST(rpc_command_annotate, test_annotation_register_invalid_parameters)
{
	const struct device *flash_tdf_logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_flash));
	struct net_buf *rsp;

	/* Empty labels are invalid. */
	send_annotation_register_command(40, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD, 0, 1, "");
	rsp = expect_rpc_response(40, RPC_ID_ANNOTATION_REGISTER,
				  INFUSE_RPC_ERROR_INVALID_ARGUMENT);
	net_buf_unref(rsp);

	/* Logger selection and readiness are validated for label registration. */
	send_annotation_register_command(41, RPC_ENUM_DATA_LOGGER_FLASH_REMOVABLE, 0, 1, "label");
	rsp = expect_rpc_response(41, RPC_ID_ANNOTATION_REGISTER,
				  INFUSE_RPC_ERROR_DEVICE_NOT_FOUND);
	net_buf_unref(rsp);
	flash_tdf_logger->state->init_res += 1;
	send_annotation_register_command(42, RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD, 0, 1, "label");
	rsp = expect_rpc_response(42, RPC_ID_ANNOTATION_REGISTER,
				  INFUSE_RPC_ERROR_DEVICE_NOT_READY);
	net_buf_unref(rsp);
	flash_tdf_logger->state->init_res -= 1;
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
