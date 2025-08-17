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
#include <infuse/tdf/definitions.h>
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

static void run_test_case(struct tdf_test_case *tdfs, size_t num_tdfs, bool idx_array)
{
	enum tdf_data_format expected;
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
		if (idx_array) {
			rc = tdf_add_core(&state, t->p.tdf_id, t->p.tdf_len, t->p.tdf_num,
					  t->p.time, i, input_buffer, TDF_DATA_FORMAT_IDX_ARRAY);
			if (t->p.tdf_num == 1) {
				/* We expect the header to always be present on IDX_ARRAY */
				total_size += 3;
			}
		} else {
			rc = tdf_add(&state, t->p.tdf_id, t->p.tdf_len, t->p.tdf_num, t->p.time,
				     t->p.period, input_buffer);
		}
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
			zassert_equal(t->expected_rc, parsed.tdf_num);
			if (idx_array) {
				zassert_equal(i, parsed.base_idx);
				expected = TDF_DATA_FORMAT_IDX_ARRAY;
			} else {
				if (t->expected_rc > 1) {
					zassert_equal(t->p.period, parsed.period);
				} else {
					zassert_equal(0, parsed.period);
				}
				expected = t->expected_rc > 1 ? TDF_DATA_FORMAT_TIME_ARRAY
							      : TDF_DATA_FORMAT_SINGLE;
			}
			zassert_equal(expected, parsed.data_type);
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
	run_test_case(tests, ARRAY_SIZE(tests), false);
	run_test_case(tests, ARRAY_SIZE(tests), true);
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
	run_test_case(tests, ARRAY_SIZE(tests), false);
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
	run_test_case(tests, ARRAY_SIZE(tests), false);
	run_test_case(tests, ARRAY_SIZE(tests), true);
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
	run_test_case(tests, ARRAY_SIZE(tests), false);
	run_test_case(tests, ARRAY_SIZE(tests), true);
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
	run_test_case(tests, ARRAY_SIZE(tests), false);
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
	run_test_case(tests, ARRAY_SIZE(tests), false);
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
	run_test_case(tests, ARRAY_SIZE(tests), false);
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
	run_test_case(tests, ARRAY_SIZE(tests), false);
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
	run_test_case(tests, ARRAY_SIZE(tests), false);
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
	run_test_case(tests, ARRAY_SIZE(tests), false);
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
	run_test_case(tests, ARRAY_SIZE(tests), false);
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
	run_test_case(tests, ARRAY_SIZE(tests), false);
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
	run_test_case(tests, ARRAY_SIZE(tests), false);
	run_test_case(tests, ARRAY_SIZE(tests), true);
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
	run_test_case(tests, ARRAY_SIZE(tests), false);
	run_test_case(tests, ARRAY_SIZE(tests), true);
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

	tdf_buffer_state_reset(&parser);
	net_buf_simple_add_le16(&parser.buf, TDF_ARRAY_IDX | 1234);
	net_buf_simple_add_u8(&parser.buf, 0x03);
	net_buf_simple_add_u8(&parser.buf, 0x12);
	net_buf_simple_add_u8(&parser.buf, 0x34);
	rc = tdf_parse(&parser, &parsed);
	zassert_equal(-EINVAL, rc);
}

ZTEST(tdf, test_tdf_parse_find_in_buf)
{
	struct tdf_buffer_state state;
	TDF_TYPE(TDF_ACC_2G) acc = {{1, 2, 3}};
	TDF_TYPE(TDF_GYR_125DPS) gyr = {{-1, -2, -3}};
	struct tdf_parsed parsed;
	int rc;

	net_buf_simple_init_with_data(&state.buf, buf, sizeof(buf));
	tdf_buffer_state_reset(&state);

	rc = TDF_ADD(&state, TDF_ACC_2G, 1, 1000, 0, &acc);
	zassert_equal(1, rc);
	rc = TDF_ADD(&state, TDF_GYR_125DPS, 1, 2000, 0, &gyr);
	zassert_equal(1, rc);

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

ZTEST(tdf, test_parse_fuzz)
{
	struct tdf_buffer_state parser;
	struct tdf_parsed parsed;
	uint8_t random_buffer[16];
	int rc;

	/* Parse random data many time, ensure no faults */
	for (int i = 0; i < 100000; i++) {
		sys_rand_get(random_buffer, sizeof(random_buffer));
		tdf_parse_start(&parser, random_buffer, sizeof(random_buffer));
		do {
			rc = tdf_parse(&parser, &parsed);
		} while (rc == 0);
	}
}

static bool test_data_init(const void *global_state)
{
	base_time = epoch_time_from(1000000, 0);
	sys_rand_get(input_buffer, sizeof(input_buffer));
	return true;
}

ZTEST_SUITE(tdf, test_data_init, NULL, NULL, NULL, NULL);

enum {
	TDF_EXAMPLE_16 = 1058,
	TDF_EXAMPLE_32 = 1059,
};

struct tdf_example_16 {
	int16_t x;
	int16_t y;
	int16_t z;
} __packed;

struct tdf_example_32 {
	int32_t x;
	int32_t y;
} __packed;

#ifdef CONFIG_TDF_DIFF

static uint8_t large_buf[512];
static uint8_t large_buf2[512];

static void validate_diff_data(struct tdf_buffer_state *state, uint8_t expected_type,
			       uint8_t expected_num, uint16_t tdf_id, const void *expected_tdfs,
			       const void *expected_diffs, size_t diff_size)
{
	const uint8_t *expected_tdfs_u8 = expected_tdfs;
	struct tdf_buffer_state parser, parser2;
	struct tdf_parsed parsed, parsed2;
	uint8_t mem_buffer[16];
	size_t expected_len, diff_len;
	int8_t *diff_data;
	int rc;

	if (tdf_id == TDF_EXAMPLE_16) {
		expected_len = sizeof(struct tdf_example_16);
	} else if (tdf_id == TDF_EXAMPLE_32) {
		expected_len = sizeof(struct tdf_example_32);
	} else {
		expected_len = 0;
		zassert_unreachable();
	}

	tdf_parse_start(&parser, state->buf.data, state->buf.len);

	rc = tdf_parse(&parser, &parsed);
	zassert_equal(0, rc);
	zassert_equal(tdf_id, parsed.tdf_id);
	zassert_equal(expected_len, parsed.tdf_len);
	zassert_equal(expected_type, parsed.data_type);

	if ((parsed.data_type == TDF_DATA_FORMAT_DIFF_ARRAY_16_8) ||
	    (parsed.data_type == TDF_DATA_FORMAT_DIFF_ARRAY_32_8) ||
	    (parsed.data_type == TDF_DATA_FORMAT_DIFF_ARRAY_32_16)) {
		if (parsed.data_type == TDF_DATA_FORMAT_DIFF_ARRAY_16_8) {
			diff_len = 3;
		} else if (parsed.data_type == TDF_DATA_FORMAT_DIFF_ARRAY_32_8) {
			diff_len = 2;
		} else if (parsed.data_type == TDF_DATA_FORMAT_DIFF_ARRAY_32_16) {
			diff_len = 4;
		} else {
			diff_len = 0;
			zassert_unreachable();
		}
		zassert_equal(expected_num, parsed.diff_info.num);
		/* Validate diff data */
		diff_data = parsed.data;
		diff_data += expected_len;
		for (int i = 0; i < parsed.diff_info.num; i++) {
			zassert_mem_equal(diff_data, expected_diffs, diff_size);
			diff_data += diff_len;
		}
		/* Validate reconstruction */
		for (int i = 0; i < (expected_num + 1); i++) {
			rc = tdf_parse_diff_reconstruct(&parsed, mem_buffer, i);
			zassert_equal(0, rc);
			zassert_mem_equal(expected_tdfs_u8 + (i * expected_len), mem_buffer,
					  expected_len);
		}
		rc = tdf_parse_diff_reconstruct(&parsed, mem_buffer, expected_num + 2);
		zassert_equal(-EINVAL, rc);

		/* Log as pre-computed diff */
		struct tdf_buffer_state relog_state;

		net_buf_simple_init_with_data(&relog_state.buf, large_buf2, sizeof(large_buf2));
		tdf_buffer_state_reset(&relog_state);

		rc = tdf_add_core(&relog_state, parsed.tdf_id, parsed.tdf_len,
				  1 + parsed.diff_info.num, parsed.time, parsed.period, parsed.data,
				  TDF_DATA_FORMAT_DIFF_PRECOMPUTED | parsed.data_type);
		zassert_equal(1 + parsed.diff_info.num, rc);

		/* Parsed re-logged data should match the original */
		tdf_parse_start(&parser2, relog_state.buf.data, relog_state.buf.len);
		rc = tdf_parse(&parser2, &parsed2);
		zassert_equal(0, rc);
		zassert_equal(parsed.tdf_id, parsed2.tdf_id);
		zassert_equal(parsed.tdf_len, parsed2.tdf_len);
		zassert_equal(parsed.data_type, parsed2.data_type);
		zassert_equal(parsed.diff_info.num, parsed2.diff_info.num);
		zassert_equal(parsed.time, parsed2.time);
		zassert_equal(parsed.period, parsed2.period);

		zassert_equal(0, parser2.buf.len);
		rc = tdf_parse(&parser2, &parsed2);
		zassert_equal(-ENOMEM, rc);
	} else {
		zassert_equal(expected_num, parsed.tdf_num);

		rc = tdf_parse_diff_reconstruct(&parsed, mem_buffer, 0);
		zassert_equal(-EINVAL, rc);
	}
	/* No more data in buffer */
	zassert_equal(0, parser.buf.len);
	rc = tdf_parse(&parser, &parsed);
	zassert_equal(-ENOMEM, rc);
}

static void tdf_diff_test(void *tdf_array, uint16_t tdf_id, uint8_t diff_type, void *diff_1,
			  void *diff_2)
{
	uint8_t *tdf_array_u8 = tdf_array;
	uint8_t *tdf_array_iter;
	struct tdf_buffer_state state;
	size_t tdf_len, diff_len;
	int array_size = 8;
	int handled;

	if (tdf_id == TDF_EXAMPLE_16) {
		tdf_len = sizeof(struct tdf_example_16);
	} else if (tdf_id == TDF_EXAMPLE_32) {
		tdf_len = sizeof(struct tdf_example_32);
	} else {
		zassert_unreachable();
	}
	if (diff_type == TDF_DATA_FORMAT_DIFF_ARRAY_16_8) {
		diff_len = 3;
	} else if (diff_type == TDF_DATA_FORMAT_DIFF_ARRAY_32_8) {
		diff_len = 2;
	} else if (diff_type == TDF_DATA_FORMAT_DIFF_ARRAY_32_16) {
		diff_len = 4;
	} else {
		diff_len = 0;
		zassert_unreachable();
	}

	/* Diff encoding requested with only a single TDF */
	net_buf_simple_init_with_data(&state.buf, buf, sizeof(buf));
	tdf_buffer_state_reset(&state);

	handled = tdf_add_core(&state, tdf_id, tdf_len, 1, 0, 10, tdf_array_u8, diff_type);
	zassert_equal(1, handled);
	validate_diff_data(&state, TDF_DATA_FORMAT_SINGLE, 1, tdf_id, NULL, NULL, 0);

	/* Diff encoding requested with two TDF's, should fallback to TDF_ARRAY_TIME */
	net_buf_simple_init_with_data(&state.buf, buf, sizeof(buf));
	tdf_buffer_state_reset(&state);

	handled = tdf_add_core(&state, tdf_id, tdf_len, 2, 0, 10, tdf_array_u8, diff_type);
	zassert_equal(2, handled);
	validate_diff_data(&state, TDF_DATA_FORMAT_TIME_ARRAY, 2, tdf_id, NULL, NULL, 0);

	/* 3 should work as expected (2 diffs) */
	net_buf_simple_init_with_data(&state.buf, buf, sizeof(buf));
	tdf_buffer_state_reset(&state);

	tdf_array_iter = tdf_array_u8;
	handled = tdf_add_core(&state, tdf_id, tdf_len, 3, 0, 10, tdf_array_u8, diff_type);
	zassert_equal(3, handled);
	validate_diff_data(&state, diff_type, 2, tdf_id, tdf_array_iter, diff_1, diff_len);

	/* Should only populate the first 4 TDFs */
	net_buf_simple_init_with_data(&state.buf, buf, sizeof(buf));
	tdf_buffer_state_reset(&state);

	tdf_array_iter = tdf_array_u8;
	handled =
		tdf_add_core(&state, tdf_id, tdf_len, array_size, 0, 10, tdf_array_iter, diff_type);
	zassert_equal(4, handled);
	validate_diff_data(&state, diff_type, 3, tdf_id, tdf_array_iter, diff_1, diff_len);

	/* Should only populate TDFs 1-4 */
	net_buf_simple_init_with_data(&state.buf, buf, sizeof(buf));
	tdf_buffer_state_reset(&state);

	tdf_array_iter = tdf_array_u8 + (1 * tdf_len);
	handled = tdf_add_core(&state, tdf_id, tdf_len, array_size - 1, 0, 10, tdf_array_iter,
			       diff_type);
	zassert_equal(3, handled);
	validate_diff_data(&state, diff_type, 2, tdf_id, tdf_array_iter, diff_1, diff_len);

	/* Should populate TDFs 3-4 as a TIME_ARRAY */
	net_buf_simple_init_with_data(&state.buf, buf, sizeof(buf));
	tdf_buffer_state_reset(&state);

	tdf_array_iter = tdf_array_u8 + (2 * tdf_len);
	handled = tdf_add_core(&state, tdf_id, tdf_len, array_size - 2, 0, 10, tdf_array_iter,
			       diff_type);
	zassert_equal(2, handled);
	validate_diff_data(&state, TDF_DATA_FORMAT_TIME_ARRAY, 2, tdf_id, NULL, NULL, 0);

	/* Should only populate TDFs 4 */
	net_buf_simple_init_with_data(&state.buf, buf, sizeof(buf));
	tdf_buffer_state_reset(&state);

	tdf_array_iter = tdf_array_u8 + (3 * tdf_len);
	handled = tdf_add_core(&state, tdf_id, tdf_len, array_size - 3, 0, 10, tdf_array_iter,
			       diff_type);
	zassert_equal(1, handled);
	validate_diff_data(&state, TDF_DATA_FORMAT_SINGLE, 1, tdf_id, NULL, NULL, 0);

	/* Should handle all remaining TDFs */
	net_buf_simple_init_with_data(&state.buf, buf, sizeof(buf));
	tdf_buffer_state_reset(&state);

	tdf_array_iter = tdf_array_u8 + (4 * tdf_len);
	handled = tdf_add_core(&state, tdf_id, tdf_len, array_size - 4, 0, 10,
			       tdf_array_u8 + (4 * tdf_len), diff_type);
	zassert_equal(4, handled);
	validate_diff_data(&state, diff_type, 3, tdf_id, tdf_array_iter, diff_2, diff_len);
}

ZTEST(tdf_diff, test_16)
{
	struct tdf_example_16 tdf_array[8] = {0};
	int8_t diff_1[3] = {10, 1, -1};
	int8_t diff_2[3] = {10, 0, 0};

	for (int i = 0; i < 4; i++) {
		tdf_array[i].x = 10 * i;
		tdf_array[i].y = i;
		tdf_array[i].z = 1 - i;
	}
	for (int i = 4; i < 8; i++) {
		tdf_array[i].x = 30000 + (10 * i);
	}

	tdf_diff_test(tdf_array, TDF_EXAMPLE_16, TDF_DATA_FORMAT_DIFF_ARRAY_16_8, diff_1, diff_2);
}

ZTEST(tdf_diff, test_32_8)
{
	struct tdf_example_32 tdf_array[8] = {0};
	int8_t diff_1[2] = {10, -1};
	int8_t diff_2[2] = {10, 0};

	for (int i = 0; i < 4; i++) {
		tdf_array[i].x = 10 * i;
		tdf_array[i].y = 1 - i;
	}
	for (int i = 4; i < 8; i++) {
		tdf_array[i].x = 30000 + (10 * i);
	}

	tdf_diff_test(tdf_array, TDF_EXAMPLE_32, TDF_DATA_FORMAT_DIFF_ARRAY_32_8, diff_1, diff_2);
}

ZTEST(tdf_diff, test_32_16)
{
	struct tdf_example_32 tdf_array[8] = {0};
	int16_t diff_1[2] = {10000, -1};
	int16_t diff_2[2] = {1200, 0};

	for (int i = 0; i < 4; i++) {
		tdf_array[i].x = 10000 * i;
		tdf_array[i].y = 1 - i;
	}
	for (int i = 4; i < 8; i++) {
		tdf_array[i].x = 3000000 + (1200 * i);
	}

	tdf_diff_test(tdf_array, TDF_EXAMPLE_32, TDF_DATA_FORMAT_DIFF_ARRAY_32_16, diff_1, diff_2);
}

ZTEST(tdf_diff, test_no_valid_diffs)
{
	struct tdf_buffer_state state;
	struct tdf_example_16 tdf_array[8] = {0};
	int handled;

	for (int i = 0; i < ARRAY_SIZE(tdf_array); i++) {
		tdf_array[i].x = -i;
		tdf_array[i].y = i;
		tdf_array[i].z = 1000 * (i % 2);
	}

	/* Diff encoding requested with only a single TDF */
	net_buf_simple_init_with_data(&state.buf, large_buf, sizeof(large_buf));
	tdf_buffer_state_reset(&state);

	handled = tdf_add_core(&state, TDF_EXAMPLE_16, sizeof(struct tdf_example_16),
			       ARRAY_SIZE(tdf_array), 0, 10, tdf_array,
			       TDF_DATA_FORMAT_DIFF_ARRAY_16_8);
	zassert_equal(ARRAY_SIZE(tdf_array), handled);
	validate_diff_data(&state, TDF_DATA_FORMAT_TIME_ARRAY, ARRAY_SIZE(tdf_array),
			   TDF_EXAMPLE_16, NULL, NULL, 0);

	/* Last 2 values have a valid diff, no change to output  */
	tdf_array[ARRAY_SIZE(tdf_array) - 1].z = -2000;
	tdf_array[ARRAY_SIZE(tdf_array) - 2].z = -2000;

	tdf_buffer_state_reset(&state);
	handled = tdf_add_core(&state, TDF_EXAMPLE_16, sizeof(struct tdf_example_16),
			       ARRAY_SIZE(tdf_array), 0, 10, tdf_array,
			       TDF_DATA_FORMAT_DIFF_ARRAY_16_8);
	zassert_equal(ARRAY_SIZE(tdf_array), handled);
	validate_diff_data(&state, TDF_DATA_FORMAT_TIME_ARRAY, ARRAY_SIZE(tdf_array),
			   TDF_EXAMPLE_16, NULL, NULL, 0);

	/* Last 3 values have valid diffs, excluded from time array */
	tdf_array[ARRAY_SIZE(tdf_array) - 3].z = -2000;

	tdf_buffer_state_reset(&state);

	handled = tdf_add_core(&state, TDF_EXAMPLE_16, sizeof(struct tdf_example_16),
			       ARRAY_SIZE(tdf_array), 0, 10, tdf_array,
			       TDF_DATA_FORMAT_DIFF_ARRAY_16_8);
	zassert_equal(ARRAY_SIZE(tdf_array) - 3, handled);
	validate_diff_data(&state, TDF_DATA_FORMAT_TIME_ARRAY, ARRAY_SIZE(tdf_array) - 3,
			   TDF_EXAMPLE_16, NULL, NULL, 0);

	/* Invalid diff type */
	tdf_buffer_state_reset(&state);

	handled = tdf_add_core(&state, TDF_EXAMPLE_16, sizeof(struct tdf_example_16),
			       ARRAY_SIZE(tdf_array), 0, 10, tdf_array, TDF_DATA_FORMAT_INVALID);
	zassert_equal(-EINVAL, handled);
}

ZTEST(tdf_diff, test_overflow)
{
	struct tdf_buffer_state state;
	struct tdf_example_16 tdf_array[128] = {0};
	uint8_t diff_1[3] = {0, 0, 0};
	int handled;

	/* Logging more diffs than can fit in the buffer */
	net_buf_simple_init_with_data(&state.buf, buf, sizeof(buf));
	tdf_buffer_state_reset(&state);

	handled = tdf_add_core(&state, TDF_EXAMPLE_16, sizeof(struct tdf_example_16), 16, 0, 10,
			       tdf_array, TDF_DATA_FORMAT_DIFF_ARRAY_16_8);
	zassert_equal(7, handled);
	validate_diff_data(&state, TDF_DATA_FORMAT_DIFF_ARRAY_16_8, 6, TDF_EXAMPLE_16, tdf_array,
			   diff_1, sizeof(diff_1));

	/* Logging more diffs than can fit in the 32 limit */
	net_buf_simple_init_with_data(&state.buf, large_buf, sizeof(large_buf));
	tdf_buffer_state_reset(&state);

	handled = tdf_add_core(&state, TDF_EXAMPLE_16, sizeof(struct tdf_example_16),
			       ARRAY_SIZE(tdf_array), 0, 10, tdf_array,
			       TDF_DATA_FORMAT_DIFF_ARRAY_16_8);
	zassert_equal(64, handled);
	validate_diff_data(&state, TDF_DATA_FORMAT_DIFF_ARRAY_16_8, 63, TDF_EXAMPLE_16, tdf_array,
			   diff_1, sizeof(diff_1));
}

ZTEST(tdf_diff, test_invalid_tdfs)
{
	struct tdf_example_16 tdf_array[128] = {0};
	struct tdf_buffer_state state;
	int rc;

	net_buf_simple_init_with_data(&state.buf, buf, sizeof(buf));
	tdf_buffer_state_reset(&state);

	rc = tdf_add_core(&state, TDF_EXAMPLE_16, 1, 16, 0, 10, tdf_array,
			  TDF_DATA_FORMAT_DIFF_ARRAY_16_8);
	zassert_equal(-EINVAL, rc);

	for (int i = 0; i < 3; i++) {
		rc = tdf_add_core(&state, TDF_EXAMPLE_32, i, 16, 0, 10, tdf_array,
				  TDF_DATA_FORMAT_DIFF_ARRAY_32_8);
		zassert_equal(-EINVAL, rc);
		rc = tdf_add_core(&state, TDF_EXAMPLE_32, i, 16, 0, 10, tdf_array,
				  TDF_DATA_FORMAT_DIFF_ARRAY_32_16);
		zassert_equal(-EINVAL, rc);
	}
}

#else

ZTEST(tdf_diff, test_disabled)
{
	struct tdf_buffer_state state;
	struct tdf_example_16 tdf_array[4] = {0};
	struct tdf_buffer_state parser;
	struct tdf_parsed parsed;
	int rc;

	/* Logging diff request without diff support */
	net_buf_simple_init_with_data(&state.buf, buf, sizeof(buf));
	tdf_buffer_state_reset(&state);

	/* Data should still be logged as a standard TIME_ARRAY */
	rc = tdf_add_core(&state, TDF_EXAMPLE_16, sizeof(struct tdf_example_16),
			  ARRAY_SIZE(tdf_array), 0, 10, tdf_array, TDF_DATA_FORMAT_DIFF_ARRAY_16_8);
	zassert_equal(ARRAY_SIZE(tdf_array), rc);

	tdf_parse_start(&parser, state.buf.data, state.buf.len);
	rc = tdf_parse(&parser, &parsed);
	zassert_equal(0, rc);
	zassert_equal(TDF_EXAMPLE_16, parsed.tdf_id);
	zassert_equal(sizeof(struct tdf_example_16), parsed.tdf_len);
	zassert_equal(TDF_DATA_FORMAT_TIME_ARRAY, parsed.data_type);
	zassert_equal(ARRAY_SIZE(tdf_array), parsed.tdf_num);

	/* No more data in buffer */
	zassert_equal(0, parser.buf.len);
	rc = tdf_parse(&parser, &parsed);
	zassert_equal(-ENOMEM, rc);
}

#endif /* CONFIG_TDF_DIFF */

ZTEST_SUITE(tdf_diff, NULL, NULL, NULL, NULL, NULL);
