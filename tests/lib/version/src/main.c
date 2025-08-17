/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>

#include <infuse/version.h>

ZTEST(version, test_version_compare)
{
	struct infuse_version a = {1, 2, 40, 10000};
	struct infuse_version b = {4, 1, 20, 0};
	struct infuse_version c = {4, 1, 20, 123};
	struct infuse_version d = {4, 1, 10, 123};
	struct infuse_version e = {4, 1, 30, 123};
	struct infuse_version f = {4, 2, 5, 123};

	zassert_equal(1, (infuse_version_compare(&a, &b)));
	zassert_equal(1, (infuse_version_compare(&a, &c)));
	zassert_equal(1, (infuse_version_compare(&a, &d)));
	zassert_equal(1, (infuse_version_compare(&a, &e)));
	zassert_equal(1, (infuse_version_compare(&a, &f)));

	zassert_equal(1, (infuse_version_compare(&d, &c)));
	zassert_equal(1, (infuse_version_compare(&d, &e)));

	zassert_equal(1, (infuse_version_compare(&c, &f)));
	zassert_equal(1, (infuse_version_compare(&d, &f)));
	zassert_equal(1, (infuse_version_compare(&e, &f)));

	zassert_equal(-1, (infuse_version_compare(&b, &a)));
	zassert_equal(-1, (infuse_version_compare(&c, &a)));
	zassert_equal(-1, (infuse_version_compare(&d, &a)));
	zassert_equal(-1, (infuse_version_compare(&e, &a)));

	zassert_equal(-1, (infuse_version_compare(&c, &d)));
	zassert_equal(-1, (infuse_version_compare(&e, &d)));

	zassert_equal(-1, (infuse_version_compare(&f, &c)));
	zassert_equal(-1, (infuse_version_compare(&f, &d)));
	zassert_equal(-1, (infuse_version_compare(&f, &e)));

	zassert_equal(0, (infuse_version_compare(&a, &a)));
	zassert_equal(0, (infuse_version_compare(&b, &b)));
	zassert_equal(0, (infuse_version_compare(&c, &c)));
	zassert_equal(0, (infuse_version_compare(&d, &d)));
	zassert_equal(0, (infuse_version_compare(&e, &e)));

	/* Build num ignored */
	zassert_equal(0, (infuse_version_compare(&b, &c)));
}

ZTEST_SUITE(version, NULL, NULL, NULL, NULL, NULL);
