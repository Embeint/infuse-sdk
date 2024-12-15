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
uint8_t large_buf[512];
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
			zassert_equal(t->expected_rc > 1 ? TDF_DATA_TYPE_TIME_ARRAY
							 : TDF_DATA_TYPE_SINGLE,
				      parsed.data_type);
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

ZTEST(tdf, test_add_multiple_long_period)
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
					.period = 131072,
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
					.period = 131072,
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
	zassert_equal(-EINVAL, tdf_add(&state, 100, 10, 2, 0, UINT32_MAX, input_buffer));
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

ZTEST(tdf, test_parse_invalid_array_type)
{
	struct tdf_buffer_state parser;
	struct tdf_parsed parsed;
	int rc;

	net_buf_simple_init_with_data(&parser.buf, buf, sizeof(buf));

	/* Invalid TDF array types (0x3000) */
	tdf_buffer_state_reset(&parser);
	net_buf_simple_add_le16(&parser.buf, 0x3000 | 1234);
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
	net_buf_simple_add_le16(&parser.buf, TDF_ARRAY_TIME | 1234);
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
	net_buf_simple_add_le16(&parser.buf, TDF_ARRAY_TIME | 1234);
	net_buf_simple_add_u8(&parser.buf, 0x03);
	net_buf_simple_add_u8(&parser.buf, 0x12);
	rc = tdf_parse(&parser, &parsed);
	zassert_equal(-EINVAL, rc);

	tdf_buffer_state_reset(&parser);
	net_buf_simple_add_le16(&parser.buf, TDF_ARRAY_TIME | 1234);
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

ZTEST(tdf_util, test_bt_addr_conv)
{
	const bt_addr_le_t addr_pub = {.type = BT_ADDR_LE_PUBLIC, .a = {{0, 1, 2, 3, 4, 5}}};
	const bt_addr_le_t addr_rnd = {.type = BT_ADDR_LE_RANDOM, .a = {{4, 5, 6, 7, 8, 9}}};
	struct tdf_struct_bt_addr_le tdf_addr;

	tdf_bt_addr_le_from_stack(&addr_pub, &tdf_addr);
	zassert_equal(BT_ADDR_LE_PUBLIC, tdf_addr.type);
	zassert_mem_equal(tdf_addr.val, addr_pub.a.val, 6);

	tdf_bt_addr_le_from_stack(&addr_rnd, &tdf_addr);
	zassert_equal(BT_ADDR_LE_RANDOM, tdf_addr.type);
	zassert_mem_equal(tdf_addr.val, addr_rnd.a.val, 6);
}

ZTEST_SUITE(tdf_util, NULL, NULL, NULL, NULL, NULL);

bool tdf_acc_2g_diff_handle(const void *current, const void *next, int8_t *out)
{
	const struct tdf_acc_2g *c = current;
	const struct tdf_acc_2g *n = next;
	int32_t sample_x = n->sample.x - c->sample.x;
	int32_t sample_y = n->sample.y - c->sample.y;
	int32_t sample_z = n->sample.z - c->sample.z;

	if (!IN_RANGE(sample_x, INT8_MIN, INT8_MAX) || !IN_RANGE(sample_y, INT8_MIN, INT8_MAX) ||
	    !IN_RANGE(sample_z, INT8_MIN, INT8_MAX)) {
		return false;
	}
	if (out != NULL) {
		out[0] = sample_x;
		out[1] = sample_y;
		out[2] = sample_z;
	}
	return true;
}

static void validate_diff_data(struct tdf_buffer_state *state, uint8_t expected_type,
			       uint8_t expected_num, uint8_t expected_diff[3])
{
	struct tdf_buffer_state parser;
	struct tdf_parsed parsed;
	uint8_t *data;
	int rc;

	tdf_parse_start(&parser, state->buf.data, state->buf.len);

	rc = tdf_parse(&parser, &parsed);
	zassert_equal(0, rc);
	zassert_equal(TDF_ACC_2G, parsed.tdf_id);
	zassert_equal(sizeof(struct tdf_acc_2g), parsed.tdf_len);
	zassert_equal(expected_type, parsed.data_type);
	if (parsed.data_type == TDF_DATA_TYPE_DIFF_ARRAY) {
		zassert_equal(expected_num, parsed.diff_num);
		data = parsed.data;
		data += sizeof(struct tdf_acc_2g);
		for (int i = 0; i < parsed.diff_num / 3; i++) {
			zassert_mem_equal(data, expected_diff, 3);
			data += 3;
		}
	} else {
		zassert_equal(expected_num, parsed.tdf_num);
	}
	/* No more data in buffer */
	zassert_equal(0, parser.buf.len);
	rc = tdf_parse(&parser, &parsed);
	zassert_equal(-ENOMEM, rc);
}

ZTEST(tdf_diff, test_basic)
{
	struct tdf_buffer_state state;
	struct tdf_acc_2g tdf_array[8] = {0};
	uint8_t diff_1[3] = {10, 1, -1};
	uint8_t diff_2[3] = {10, 0, 0};
	int handled;

	for (int i = 0; i < 4; i++) {
		tdf_array[i].sample.x = 10 * i;
		tdf_array[i].sample.y = i;
		tdf_array[i].sample.z = -i;
	}
	for (int i = 4; i < 8; i++) {
		tdf_array[i].sample.x = 30000 + (10 * i);
	}

	/* Diff encoding requested with only a single TDF */
	net_buf_simple_init_with_data(&state.buf, buf, sizeof(buf));
	tdf_buffer_state_reset(&state);

	handled = tdf_add_diff(&state, TDF_ACC_2G, sizeof(struct tdf_acc_2g), 1, 0, 10, tdf_array,
			       3, tdf_acc_2g_diff_handle);
	zassert_equal(1, handled);
	validate_diff_data(&state, TDF_DATA_TYPE_SINGLE, 1, NULL);

	/* Should only populate the first 4 TDFs */
	net_buf_simple_init_with_data(&state.buf, buf, sizeof(buf));
	tdf_buffer_state_reset(&state);

	handled = tdf_add_diff(&state, TDF_ACC_2G, sizeof(struct tdf_acc_2g), ARRAY_SIZE(tdf_array),
			       0, 10, tdf_array, 3, tdf_acc_2g_diff_handle);
	zassert_equal(4, handled);
	validate_diff_data(&state, TDF_DATA_TYPE_DIFF_ARRAY, 3 * 3, diff_1);

	/* Should only populate TDFs 1-4 */
	net_buf_simple_init_with_data(&state.buf, buf, sizeof(buf));
	tdf_buffer_state_reset(&state);

	handled = tdf_add_diff(&state, TDF_ACC_2G, sizeof(struct tdf_acc_2g),
			       ARRAY_SIZE(tdf_array) - 1, 0, 10, tdf_array + 1, 3,
			       tdf_acc_2g_diff_handle);
	zassert_equal(3, handled);
	validate_diff_data(&state, TDF_DATA_TYPE_DIFF_ARRAY, 2 * 3, diff_1);

	/* Should populate TDFs 3-4 as a TIME_ARRAY */
	net_buf_simple_init_with_data(&state.buf, buf, sizeof(buf));
	tdf_buffer_state_reset(&state);

	handled = tdf_add_diff(&state, TDF_ACC_2G, sizeof(struct tdf_acc_2g),
			       ARRAY_SIZE(tdf_array) - 2, 0, 10, tdf_array + 2, 3,
			       tdf_diff_encode_acc_2g);
	zassert_equal(2, handled);
	validate_diff_data(&state, TDF_DATA_TYPE_TIME_ARRAY, 2, NULL);

	/* Should only populate TDFs 4 */
	net_buf_simple_init_with_data(&state.buf, buf, sizeof(buf));
	tdf_buffer_state_reset(&state);

	handled = tdf_add_diff(&state, TDF_ACC_2G, sizeof(struct tdf_acc_2g),
			       ARRAY_SIZE(tdf_array) - 3, 0, 10, tdf_array + 3, 3,
			       tdf_acc_2g_diff_handle);
	zassert_equal(1, handled);
	validate_diff_data(&state, TDF_DATA_TYPE_SINGLE, 1, NULL);

	/* Should handle all remaining TDFs */
	net_buf_simple_init_with_data(&state.buf, buf, sizeof(buf));
	tdf_buffer_state_reset(&state);

	handled = tdf_add_diff(&state, TDF_ACC_2G, sizeof(struct tdf_acc_2g),
			       ARRAY_SIZE(tdf_array) - 4, 0, 10, tdf_array + 4, 3,
			       tdf_acc_2g_diff_handle);
	zassert_equal(4, handled);
	validate_diff_data(&state, TDF_DATA_TYPE_DIFF_ARRAY, 3 * 3, diff_2);
}

ZTEST(tdf_diff, test_no_valid_diffs)
{
	struct tdf_buffer_state state;
	struct tdf_acc_2g tdf_array[8] = {0};
	int handled;

	for (int i = 0; i < ARRAY_SIZE(tdf_array); i++) {
		tdf_array[i].sample.x = -i;
		tdf_array[i].sample.y = i;
		tdf_array[i].sample.z = 1000 * (i % 2);
	}

	/* Diff encoding requested with only a single TDF */
	net_buf_simple_init_with_data(&state.buf, large_buf, sizeof(large_buf));
	tdf_buffer_state_reset(&state);

	handled = tdf_add_diff(&state, TDF_ACC_2G, sizeof(struct tdf_acc_2g), ARRAY_SIZE(tdf_array),
			       0, 10, tdf_array, 3, tdf_diff_encode_acc_2g);
	zassert_equal(ARRAY_SIZE(tdf_array), handled);
	validate_diff_data(&state, TDF_DATA_TYPE_TIME_ARRAY, ARRAY_SIZE(tdf_array), NULL);

	/* Last 2 values have a valid diff, no change to output  */
	tdf_array[ARRAY_SIZE(tdf_array) - 1].sample.z = -2000;
	tdf_array[ARRAY_SIZE(tdf_array) - 2].sample.z = -2000;

	tdf_buffer_state_reset(&state);
	handled = tdf_add_diff(&state, TDF_ACC_2G, sizeof(struct tdf_acc_2g), ARRAY_SIZE(tdf_array),
			       0, 10, tdf_array, 3, tdf_diff_encode_acc_2g);
	zassert_equal(ARRAY_SIZE(tdf_array), handled);
	validate_diff_data(&state, TDF_DATA_TYPE_TIME_ARRAY, ARRAY_SIZE(tdf_array), NULL);

	/* Last 3 values have valid diffs, excluded from time array */
	tdf_array[ARRAY_SIZE(tdf_array) - 3].sample.z = -2000;

	tdf_buffer_state_reset(&state);

	handled = tdf_add_diff(&state, TDF_ACC_2G, sizeof(struct tdf_acc_2g), ARRAY_SIZE(tdf_array),
			       0, 10, tdf_array, 3, tdf_diff_encode_acc_2g);
	zassert_equal(ARRAY_SIZE(tdf_array) - 3, handled);
	validate_diff_data(&state, TDF_DATA_TYPE_TIME_ARRAY, ARRAY_SIZE(tdf_array) - 3, NULL);
}

ZTEST(tdf_diff, test_overflow)
{
	struct tdf_buffer_state state;
	struct tdf_acc_2g tdf_array[128] = {0};
	uint8_t diff_1[3] = {0, 0, 0};
	int handled;

	/* Logging more diffs than can fit in the buffer */
	net_buf_simple_init_with_data(&state.buf, buf, sizeof(buf));
	tdf_buffer_state_reset(&state);

	handled = tdf_add_diff(&state, TDF_ACC_2G, sizeof(struct tdf_acc_2g), 16, 0, 10, tdf_array,
			       3, tdf_acc_2g_diff_handle);
	zassert_equal(7, handled);
	validate_diff_data(&state, TDF_DATA_TYPE_DIFF_ARRAY, 6 * 3, diff_1);

	/* Logging more diffs than can fit in the UINT8_T limit */
	net_buf_simple_init_with_data(&state.buf, large_buf, sizeof(large_buf));
	tdf_buffer_state_reset(&state);

	handled = tdf_add_diff(&state, TDF_ACC_2G, sizeof(struct tdf_acc_2g), ARRAY_SIZE(tdf_array),
			       0, 10, tdf_array, 3, tdf_acc_2g_diff_handle);
	zassert_equal(85, handled);
	validate_diff_data(&state, TDF_DATA_TYPE_DIFF_ARRAY, 84 * 3, diff_1);
}

ZTEST_SUITE(tdf_diff, NULL, NULL, NULL, NULL, NULL);
