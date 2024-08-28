/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <infuse/tdf/tdf.h>
#include <infuse/tdf/util.h>
#include <infuse/time/epoch.h>

struct tdf_test_case {
	struct tdf_parsed p;
	uint16_t expected_size;
	int expected_rc;
};

static uint8_t buf[32];
static uint8_t input_buffer[128] = {0};
static uint64_t base_time;

#define __DEBUG__ 0

static void run_test_case(struct tdf_test_case *tdfs, size_t num_tdfs)
{
	struct tdf_buffer_state state;
	struct tdf_buffer_state parser;
	struct tdf_parsed parsed;
	struct tdf_test_case *t;
	size_t total_size = 0;
	int rc;

	net_buf_simple_init_with_data(&state.buf, buf, sizeof(buf));
	tdf_buffer_state_reset(&state);

	/* Add the requested TDFs */
	for (int i = 0; i < num_tdfs; i++) {
		t = &tdfs[i];
		rc = tdf_add(&state, t->p.tdf_id, t->p.tdf_len, t->p.tdf_num, t->p.time,
			     t->p.period, input_buffer);
		total_size += t->expected_size;
		zassert_equal(t->expected_rc, rc);
		zassert_equal(total_size, state.buf.len);
	}

#if __DEBUG__
	printk("BUFFER: ");
	for (int i = 0; i < state.buf.len; i++) {
		printk("%02x", state.buf.data[i]);
	}
	printk("\n");
#endif /* __DEBUG__ */

	/* Validate the data in the buffers */
	tdf_parse_start(&parser, state.buf.data, state.buf.len);
	for (int i = 0; i < num_tdfs; i++) {
		t = &tdfs[i];
		rc = tdf_parse(&parser, &parsed);
		if (t->expected_rc == -ENOMEM) {
			zassert_equal(-ENOMEM, rc);
		} else {
			zassert_equal(0, rc);
			zassert_equal(t->p.time, parsed.time);
			zassert_equal(t->p.tdf_id, parsed.tdf_id);
			zassert_equal(t->p.tdf_len, parsed.tdf_len);
			if (t->expected_rc > 1) {
				zassert_equal(t->p.period, parsed.period);
			} else {
				zassert_equal(0, parsed.period);
			}
			zassert_equal(t->expected_rc, parsed.tdf_num);
			zassert_mem_equal(input_buffer, parsed.data, parsed.tdf_len);
#if __DEBUG__
			printk("TDF %d:\n", i);
			printk("\t     ID: %d\n", parsed.tdf_id);
			printk("\t   Time: %lld\n", parsed.time);
			printk("\t Length: %d\n", parsed.tdf_len);
			if (t->expected_rc > 1) {
				printk("\t    Num: %d\n", parsed.tdf_num);
				printk("\t Period: %d\n", parsed.period);
			}
#endif /* __DEBUG__ */
		}
	}

	rc = tdf_parse(&parser, &parsed);
	zassert_equal(-ENOMEM, rc);
}

ZTEST(tdf, test_single_no_timestamp)
{
	/* TDFs with no timestamp */
	struct tdf_test_case tests[] = {
		{
			.p =
				{
					.time = 0,
					.tdf_id = 100,
					.tdf_num = 1,
					.tdf_len = 4,
				},
			.expected_size = 7,
			.expected_rc = 1,
		},
		{
			.p =
				{
					.time = 0,
					.tdf_id = 100,
					.tdf_num = 1,
					.tdf_len = 4,
				},
			.expected_size = 7,
			.expected_rc = 1,
		},
	};
	run_test_case(tests, ARRAY_SIZE(tests));
}

ZTEST(tdf, test_single_relative)
{
	/* TDFs with timestamps */
	struct tdf_test_case tests[] = {
		{
			.p =
				{
					.time = base_time,
					.tdf_id = 101,
					.tdf_num = 1,
					.tdf_len = 4,
				},
			.expected_size = 13,
			.expected_rc = 1,
		},
		{
			.p =
				{
					.time = base_time,
					.tdf_id = 102,
					.tdf_num = 1,
					.tdf_len = 4,
				},
			.expected_size = 9,
			.expected_rc = 1,
		},
		{
			.p =
				{
					.time = base_time + 100,
					.tdf_id = 103,
					.tdf_num = 1,
					.tdf_len = 4,
				},
			.expected_size = 9,
			.expected_rc = 1,
		},
		{
			.p =
				{
					.time = base_time + 100,
					.tdf_id = 104,
					.tdf_num = 1,
					.tdf_len = 4,
				},
			.expected_size = 0,
			.expected_rc = -ENOMEM,
		},
	};
	run_test_case(tests, ARRAY_SIZE(tests));
}

ZTEST(tdf, test_single_extended_jump)
{
	/* TDFs with extended time jump forward */
	struct tdf_test_case tests[] = {
		{
			.p =
				{
					.time = base_time,
					.tdf_id = 110,
					.tdf_num = 1,
					.tdf_len = 4,
				},
			.expected_size = 13,
			.expected_rc = 1,
		},
		{
			.p =
				{
					.time = base_time + 100000,
					.tdf_id = 111,
					.tdf_num = 1,
					.tdf_len = 4,
				},
			.expected_size = 10,
			.expected_rc = 1,
		},
	};
	run_test_case(tests, ARRAY_SIZE(tests));
}

ZTEST(tdf, test_single_jump_backwards)
{
	/* TDFs with time jump backward */
	struct tdf_test_case tests[] = {
		{
			.p =
				{
					.time = base_time,
					.tdf_id = 50,
					.tdf_num = 1,
					.tdf_len = 6,
				},
			.expected_size = 15,
			.expected_rc = 1,
		},
		{
			.p =
				{
					.time = base_time - 1,
					.tdf_id = 55,
					.tdf_num = 1,
					.tdf_len = 4,
				},
			.expected_size = 10,
			.expected_rc = 1,
		},
	};
	run_test_case(tests, ARRAY_SIZE(tests));
}

ZTEST(tdf, test_single_large_jump)
{
	/* TDFs with very large jump forward in time */
	struct tdf_test_case tests[] = {
		{
			.p =
				{
					.time = base_time,
					.tdf_id = 20,
					.tdf_num = 1,
					.tdf_len = 4,
				},
			.expected_size = 13,
			.expected_rc = 1,
		},
		{
			.p =
				{
					.time = base_time + UINT32_MAX,
					.tdf_id = 19,
					.tdf_num = 1,
					.tdf_len = 4,
				},
			.expected_size = 13,
			.expected_rc = 1,
		},
		{
			.p =
				{
					.time = base_time + 100,
					.tdf_id = 104,
					.tdf_num = 1,
					.tdf_len = 4,
				},
			.expected_size = 0,
			.expected_rc = -ENOMEM,
		},
	};
	run_test_case(tests, ARRAY_SIZE(tests));
}

ZTEST(tdf, test_single_large_jump_back)
{
	/* TDFs with very large jump backwards in time */
	struct tdf_test_case tests[] = {
		{
			.p =
				{
					.time = base_time,
					.tdf_id = 100,
					.tdf_num = 1,
					.tdf_len = 4,
				},
			.expected_size = 13,
			.expected_rc = 1,
		},
		{
			.p =
				{
					.time = base_time - UINT32_MAX,
					.tdf_id = 19,
					.tdf_num = 1,
					.tdf_len = 4,
				},
			.expected_size = 13,
			.expected_rc = 1,
		},
		{
			.p =
				{
					.time = base_time + 100,
					.tdf_id = 104,
					.tdf_num = 1,
					.tdf_len = 4,
				},
			.expected_size = 0,
			.expected_rc = -ENOMEM,
		},
	};
	run_test_case(tests, ARRAY_SIZE(tests));
}

ZTEST(tdf, test_single_multiple_jumps)
{
	/* TDFs with multiple jumps that combined are over UINT16_MAX */
	struct tdf_test_case tests[] = {
		{
			.p =
				{
					.time = base_time,
					.tdf_id = 100,
					.tdf_num = 1,
					.tdf_len = 4,
				},
			.expected_size = 13,
			.expected_rc = 1,
		},
		{
			.p =
				{
					.time = base_time + UINT16_MAX,
					.tdf_id = 100,
					.tdf_num = 1,
					.tdf_len = 4,
				},
			.expected_size = 9,
			.expected_rc = 1,
		},
		{
			.p =
				{
					.time = base_time + 2 * UINT16_MAX,
					.tdf_id = 100,
					.tdf_num = 1,
					.tdf_len = 4,
				},
			.expected_size = 9,
			.expected_rc = 1,
		},
		{
			.p =
				{
					.time = base_time + 3 * UINT16_MAX,
					.tdf_id = 104,
					.tdf_num = 1,
					.tdf_len = 4,
				},
			.expected_size = 0,
			.expected_rc = -ENOMEM,
		},
	};
	run_test_case(tests, ARRAY_SIZE(tests));
}

ZTEST(tdf, test_add_multiple)
{
	/* Multiple TDFs */
	struct tdf_test_case tests[] = {
		{
			.p =
				{
					.time = 0,
					.tdf_id = 100,
					.tdf_num = 2,
					.tdf_len = 4,
					.period = 100,
				},
			.expected_size = 14,
			.expected_rc = 2,
		},
		{
			.p =
				{
					.time = 0,
					.tdf_id = 100,
					.tdf_num = 2,
					.tdf_len = 4,
					.period = 10,
				},
			.expected_size = 14,
			.expected_rc = 2,
		},
		{
			.p =
				{
					.time = 0,
					.tdf_id = 104,
					.tdf_num = 1,
					.tdf_len = 4,
				},
			.expected_size = 0,
			.expected_rc = -ENOMEM,
		},
	};
	run_test_case(tests, ARRAY_SIZE(tests));
}

ZTEST(tdf, test_multiple_too_many)
{
	/* More TDFs than fit on the buffer */
	struct tdf_test_case tests[] = {
		{
			.p =
				{
					.time = 0,
					.tdf_id = 100,
					.tdf_num = 8,
					.tdf_len = 4,
					.period = 150,
				},
			.expected_size = 30,
			.expected_rc = 6,
		},
		{
			.p =
				{
					.time = 0,
					.tdf_id = 104,
					.tdf_num = 1,
					.tdf_len = 4,
				},
			.expected_size = 0,
			.expected_rc = -ENOMEM,
		},
	};
	run_test_case(tests, ARRAY_SIZE(tests));
}

ZTEST(tdf, test_add_multiple_2_to_1)
{
	/* Going from 2 to 1 */
	struct tdf_test_case tests[] = {
		{
			.p =
				{
					.time = 0,
					.tdf_id = 4000,
					.tdf_num = 2,
					.tdf_len = 16,
					.period = 150,
				},
			.expected_size = 19,
			.expected_rc = 1,
		},
	};
	run_test_case(tests, ARRAY_SIZE(tests));
}

ZTEST(tdf, test_add_multiple_2_to_1_exact)
{
	/* Exactly enough space to go from 2 to 1 */
	struct tdf_test_case tests[] = {
		{
			.p =
				{
					.time = 0,
					.tdf_id = 2000,
					.tdf_num = 2,
					.tdf_len = 29,
					.period = 200,
				},
			.expected_size = 32,
			.expected_rc = 1,
		},
	};
	run_test_case(tests, ARRAY_SIZE(tests));
}

ZTEST(tdf, test_add_no_time_to_time)
{
	/* No timestamp then timestamp */
	struct tdf_test_case tests[] = {
		{
			.p =
				{
					.time = 0,
					.tdf_id = 100,
					.tdf_num = 1,
					.tdf_len = 4,
				},
			.expected_size = 7,
			.expected_rc = 1,
		},
		{
			.p =
				{
					.time = base_time,
					.tdf_id = 100,
					.tdf_num = 1,
					.tdf_len = 4,
				},
			.expected_size = 13,
			.expected_rc = 1,
		},
	};
	run_test_case(tests, ARRAY_SIZE(tests));
}

ZTEST(tdf, test_add_time_to_no_time)
{
	/* No timestamp then timestamp */
	struct tdf_test_case tests[] = {
		{
			.p =
				{
					.time = base_time,
					.tdf_id = 100,
					.tdf_num = 1,
					.tdf_len = 4,
				},
			.expected_size = 13,
			.expected_rc = 1,
		},
		{
			.p =
				{
					.time = 0,
					.tdf_id = 100,
					.tdf_num = 1,
					.tdf_len = 4,
				},
			.expected_size = 7,
			.expected_rc = 1,
		},
	};
	run_test_case(tests, ARRAY_SIZE(tests));
}

ZTEST(tdf, test_invalid_params)
{
	struct tdf_buffer_state state;

	net_buf_simple_init_with_data(&state.buf, buf, sizeof(buf));
	tdf_buffer_state_reset(&state);

	zassert_equal(-EINVAL, tdf_add(&state, 0, 10, 1, 0, 0, input_buffer));
	zassert_equal(-EINVAL, tdf_add(&state, UINT16_MAX, 10, 1, 0, 0, input_buffer));
	zassert_equal(-EINVAL, tdf_add(&state, 100, 0, 1, 0, 0, input_buffer));
	zassert_equal(-EINVAL, tdf_add(&state, 100, 10, 0, 0, 0, input_buffer));
}

ZTEST(tdf, test_invalid_sizes)
{
	struct tdf_buffer_state state;
	int rc;

	net_buf_simple_init_with_data(&state.buf, buf, sizeof(buf));
	tdf_buffer_state_reset(&state);

	/* Too large to ever fit without a timestamp */
	for (int i = 30; i < 64; i++) {
		rc = tdf_add(&state, 10, i, 1, 0, 0, input_buffer);
		zassert_equal(-ENOSPC, rc);
	}
	/* Too large to ever fit with a timestamp */
	for (int i = 24; i < 64; i++) {
		rc = tdf_add(&state, 10, i, 1, 1000, 0, input_buffer);
		zassert_equal(-ENOSPC, rc);
	}

	/* Reserve space at start of buffer */
	net_buf_simple_reserve(&state.buf, 2);

	/* Too large to ever fit without a timestamp */
	for (int i = 28; i < 64; i++) {
		rc = tdf_add(&state, 10, i, 1, 0, 0, input_buffer);
		zassert_equal(-ENOSPC, rc);
	}
	/* Too large to ever fit with a timestamp */
	for (int i = 22; i < 64; i++) {
		rc = tdf_add(&state, 10, i, 1, 1000, 0, input_buffer);
		zassert_equal(-ENOSPC, rc);
	}
}

ZTEST(tdf, test_parse_invalid_lengths)
{
	struct tdf_buffer_state parser;
	struct tdf_parsed parsed;
	int rc;

	net_buf_simple_init_with_data(&parser.buf, buf, sizeof(buf));

	/* Invalid lengths */
	tdf_buffer_state_reset(&parser);
	net_buf_simple_add_le16(&parser.buf, 0x0000);
	rc = tdf_parse(&parser, &parsed);
	zassert_equal(-ENOMEM, rc);

	tdf_buffer_state_reset(&parser);
	net_buf_simple_add_le16(&parser.buf, 0xFFFF);
	rc = tdf_parse(&parser, &parsed);
	zassert_equal(-ENOMEM, rc);

	tdf_buffer_state_reset(&parser);
	net_buf_simple_add_le24(&parser.buf, 0xFFFF00);
	rc = tdf_parse(&parser, &parsed);
	zassert_equal(-ENOMEM, rc);
}

ZTEST(tdf, test_parse_invalid_ids)
{
	struct tdf_buffer_state parser;
	struct tdf_parsed parsed;
	int rc;

	net_buf_simple_init_with_data(&parser.buf, buf, sizeof(buf));

	/* Invalid TDF IDs */
	tdf_buffer_state_reset(&parser);
	net_buf_simple_add_le16(&parser.buf, 0x0000);
	net_buf_simple_add_u8(&parser.buf, 0x01);
	net_buf_simple_add_u8(&parser.buf, 0xFF);
	rc = tdf_parse(&parser, &parsed);
	zassert_equal(-EINVAL, rc);

	tdf_buffer_state_reset(&parser);
	net_buf_simple_add_le16(&parser.buf, 0xFFFF);
	net_buf_simple_add_u8(&parser.buf, 0x01);
	net_buf_simple_add_u8(&parser.buf, 0xFF);
	rc = tdf_parse(&parser, &parsed);
	zassert_equal(-EINVAL, rc);
}

ZTEST(tdf, test_parse_relative_without_absolute)
{
	struct tdf_buffer_state parser;
	struct tdf_parsed parsed;
	int rc;

	net_buf_simple_init_with_data(&parser.buf, buf, sizeof(buf));

	/* Relative timestamps without absolute reference */
	tdf_buffer_state_reset(&parser);
	net_buf_simple_add_le16(&parser.buf, TDF_TIMESTAMP_RELATIVE | 1234);
	net_buf_simple_add_u8(&parser.buf, 0x02);
	net_buf_simple_add_le16(&parser.buf, 0x1234);
	net_buf_simple_add_le16(&parser.buf, 0x1234);
	rc = tdf_parse(&parser, &parsed);
	zassert_equal(-EINVAL, rc);

	tdf_buffer_state_reset(&parser);
	net_buf_simple_add_le16(&parser.buf, TDF_TIMESTAMP_EXTENDED_RELATIVE | 1234);
	net_buf_simple_add_u8(&parser.buf, 0x02);
	net_buf_simple_add_le24(&parser.buf, 0x123456);
	net_buf_simple_add_le16(&parser.buf, 0x1234);
	rc = tdf_parse(&parser, &parsed);
	zassert_equal(-EINVAL, rc);

	tdf_buffer_state_reset(&parser);
	net_buf_simple_add_le16(&parser.buf, 1234);
	net_buf_simple_add_u8(&parser.buf, 0x01);
	net_buf_simple_add_u8(&parser.buf, 0xFF);
	net_buf_simple_add_le16(&parser.buf, TDF_TIMESTAMP_EXTENDED_RELATIVE | 1234);
	net_buf_simple_add_u8(&parser.buf, 0x02);
	net_buf_simple_add_le24(&parser.buf, 0x123456);
	net_buf_simple_add_le16(&parser.buf, 0x1234);
	rc = tdf_parse(&parser, &parsed);
	zassert_equal(0, rc);
	rc = tdf_parse(&parser, &parsed);
	zassert_equal(-EINVAL, rc);
}

ZTEST(tdf, test_parse_missing_payload)
{
	struct tdf_buffer_state parser;
	struct tdf_parsed parsed;
	int rc;

	net_buf_simple_init_with_data(&parser.buf, buf, sizeof(buf));

	/* Missing TDF data */
	tdf_buffer_state_reset(&parser);
	net_buf_simple_add_le16(&parser.buf, 1234);
	net_buf_simple_add_u8(&parser.buf, 0x03);
	net_buf_simple_add_le16(&parser.buf, 0x1234);
	rc = tdf_parse(&parser, &parsed);
	zassert_equal(-EINVAL, rc);

	tdf_buffer_state_reset(&parser);
	net_buf_simple_add_le16(&parser.buf, TDF_TIMESTAMP_ABSOLUTE | 1234);
	net_buf_simple_add_u8(&parser.buf, 0x03);
	net_buf_simple_add_le32(&parser.buf, 0x12345678);
	net_buf_simple_add_le16(&parser.buf, 0x1234);
	net_buf_simple_add_le16(&parser.buf, 0x1234);
	rc = tdf_parse(&parser, &parsed);
	zassert_equal(-EINVAL, rc);

	tdf_buffer_state_reset(&parser);
	net_buf_simple_add_le16(&parser.buf, TDF_TIME_ARRAY | 1234);
	net_buf_simple_add_u8(&parser.buf, 0x03);
	net_buf_simple_add_u8(&parser.buf, 0x02);
	net_buf_simple_add_le16(&parser.buf, 0x1234);
	net_buf_simple_add_le24(&parser.buf, 0x123456);
	net_buf_simple_add_le16(&parser.buf, 0x1234);
	rc = tdf_parse(&parser, &parsed);
	zassert_equal(-EINVAL, rc);
}

ZTEST(tdf, test_parse_missing_timestamps)
{
	struct tdf_buffer_state parser;
	struct tdf_parsed parsed;
	int rc;

	net_buf_simple_init_with_data(&parser.buf, buf, sizeof(buf));

	tdf_buffer_state_reset(&parser);
	net_buf_simple_add_le16(&parser.buf, TDF_TIMESTAMP_ABSOLUTE | 1234);
	net_buf_simple_add_u8(&parser.buf, 0x03);
	net_buf_simple_add_le16(&parser.buf, 0x1234);
	rc = tdf_parse(&parser, &parsed);
	zassert_equal(-EINVAL, rc);

	tdf_buffer_state_reset(&parser);
	net_buf_simple_add_le16(&parser.buf, TDF_TIMESTAMP_ABSOLUTE | 1234);
	net_buf_simple_add_u8(&parser.buf, 0x03);
	net_buf_simple_add_le32(&parser.buf, 0x12345678);
	rc = tdf_parse(&parser, &parsed);
	zassert_equal(-EINVAL, rc);

	tdf_buffer_state_reset(&parser);
	net_buf_simple_add_le16(&parser.buf, TDF_TIMESTAMP_ABSOLUTE | 1234);
	net_buf_simple_add_u8(&parser.buf, 0x03);
	net_buf_simple_add_le32(&parser.buf, 0x12345678);
	net_buf_simple_add_u8(&parser.buf, 0x12);
	rc = tdf_parse(&parser, &parsed);
	zassert_equal(-EINVAL, rc);

	tdf_buffer_state_reset(&parser);
	net_buf_simple_add_le16(&parser.buf, TDF_TIMESTAMP_RELATIVE | 1234);
	net_buf_simple_add_u8(&parser.buf, 0x03);
	net_buf_simple_add_u8(&parser.buf, 0x12);
	rc = tdf_parse(&parser, &parsed);
	zassert_equal(-EINVAL, rc);

	tdf_buffer_state_reset(&parser);
	net_buf_simple_add_le16(&parser.buf, TDF_TIMESTAMP_EXTENDED_RELATIVE | 1234);
	net_buf_simple_add_u8(&parser.buf, 0x03);
	net_buf_simple_add_le16(&parser.buf, 0x1234);
	rc = tdf_parse(&parser, &parsed);
	zassert_equal(-EINVAL, rc);
}

ZTEST(tdf, test_parse_missing_array_info)
{
	struct tdf_buffer_state parser;
	struct tdf_parsed parsed;
	int rc;

	net_buf_simple_init_with_data(&parser.buf, buf, sizeof(buf));

	tdf_buffer_state_reset(&parser);
	net_buf_simple_add_le16(&parser.buf, TDF_TIME_ARRAY | 1234);
	net_buf_simple_add_u8(&parser.buf, 0x03);
	net_buf_simple_add_u8(&parser.buf, 0x12);
	rc = tdf_parse(&parser, &parsed);
	zassert_equal(-EINVAL, rc);

	tdf_buffer_state_reset(&parser);
	net_buf_simple_add_le16(&parser.buf, TDF_TIME_ARRAY | 1234);
	net_buf_simple_add_u8(&parser.buf, 0x03);
	net_buf_simple_add_u8(&parser.buf, 0x12);
	net_buf_simple_add_u8(&parser.buf, 0x34);
	rc = tdf_parse(&parser, &parsed);
	zassert_equal(-EINVAL, rc);
}

ZTEST(tdf, test_tdf_parse_find_in_buf)
{
	struct tdf_buffer_state state;
	struct tdf_acc_2g acc = {{1, 2, 3}};
	struct tdf_gyr_125dps gyr = {{-1, -2, -3}};
	struct tdf_parsed parsed;

	net_buf_simple_init_with_data(&state.buf, buf, sizeof(buf));
	tdf_buffer_state_reset(&state);

	tdf_add(&state, TDF_ACC_2G, sizeof(acc), 1, 1000, 0, &acc);
	tdf_add(&state, TDF_GYR_125DPS, sizeof(gyr), 1, 2000, 0, &gyr);

	/* TDFs that don't exist in the buffer */
	zassert_equal(-ENOMEM,
		      tdf_parse_find_in_buf(state.buf.data, state.buf.len, TDF_ACC_4G, &parsed));
	zassert_equal(-ENOMEM, tdf_parse_find_in_buf(state.buf.data, state.buf.len, 1234, &parsed));

	/* TDFs that do exist in the buffer */
	zassert_equal(0, tdf_parse_find_in_buf(state.buf.data, state.buf.len, TDF_ACC_2G, &parsed));
	zassert_equal(1000, parsed.time);
	zassert_equal(
		0, tdf_parse_find_in_buf(state.buf.data, state.buf.len, TDF_GYR_125DPS, &parsed));
	zassert_equal(2000, parsed.time);

	/* Test corrupt buffer */
	net_buf_simple_add_u8(&state.buf, 0x00);
	zassert_equal(-ENOMEM, tdf_parse_find_in_buf(state.buf.data, state.buf.len, 1234, &parsed));
}

static bool test_data_init(const void *global_state)
{
	base_time = epoch_time_from(1000000, 0);
	sys_rand_get(input_buffer, sizeof(input_buffer));
	return true;
}

ZTEST_SUITE(tdf, test_data_init, NULL, NULL, NULL, NULL);

ZTEST(tdf_util, test_acc_range_to_tdf)
{
	zassert_equal(TDF_ACC_2G, tdf_id_from_accelerometer_range(2));
	zassert_equal(TDF_ACC_4G, tdf_id_from_accelerometer_range(4));
	zassert_equal(TDF_ACC_8G, tdf_id_from_accelerometer_range(8));
	zassert_equal(TDF_ACC_16G, tdf_id_from_accelerometer_range(16));
}

ZTEST(tdf_util, test_gyro_range_to_tdf)
{
	zassert_equal(TDF_GYR_125DPS, tdf_id_from_gyroscope_range(125));
	zassert_equal(TDF_GYR_250DPS, tdf_id_from_gyroscope_range(250));
	zassert_equal(TDF_GYR_500DPS, tdf_id_from_gyroscope_range(500));
	zassert_equal(TDF_GYR_1000DPS, tdf_id_from_gyroscope_range(1000));
	zassert_equal(TDF_GYR_2000DPS, tdf_id_from_gyroscope_range(2000));
}

ZTEST_SUITE(tdf_util, NULL, NULL, NULL, NULL, NULL);
