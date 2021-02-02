/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DAMON Debugfs Interface Unit Tests
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 */

#ifdef CONFIG_DAMON_DBGFS_KUNIT_TEST

#ifndef _DAMON_DBGFS_TEST_H
#define _DAMON_DBGFS_TEST_H

#include <kunit/test.h>

static void damon_dbgfs_test_str_to_target_ids(struct kunit *test)
{
	char *question;
	unsigned long *answers;
	unsigned long expected[] = {12, 35, 46};
	ssize_t nr_integers = 0, i;

	question = "123";
	answers = str_to_target_ids(question, strnlen(question, 128),
			&nr_integers);
	KUNIT_EXPECT_EQ(test, (ssize_t)1, nr_integers);
	KUNIT_EXPECT_EQ(test, 123ul, answers[0]);
	kfree(answers);

	question = "123abc";
	answers = str_to_target_ids(question, strnlen(question, 128),
			&nr_integers);
	KUNIT_EXPECT_EQ(test, (ssize_t)1, nr_integers);
	KUNIT_EXPECT_EQ(test, 123ul, answers[0]);
	kfree(answers);

	question = "a123";
	answers = str_to_target_ids(question, strnlen(question, 128),
			&nr_integers);
	KUNIT_EXPECT_EQ(test, (ssize_t)0, nr_integers);
	kfree(answers);

	question = "12 35";
	answers = str_to_target_ids(question, strnlen(question, 128),
			&nr_integers);
	KUNIT_EXPECT_EQ(test, (ssize_t)2, nr_integers);
	for (i = 0; i < nr_integers; i++)
		KUNIT_EXPECT_EQ(test, expected[i], answers[i]);
	kfree(answers);

	question = "12 35 46";
	answers = str_to_target_ids(question, strnlen(question, 128),
			&nr_integers);
	KUNIT_EXPECT_EQ(test, (ssize_t)3, nr_integers);
	for (i = 0; i < nr_integers; i++)
		KUNIT_EXPECT_EQ(test, expected[i], answers[i]);
	kfree(answers);

	question = "12 35 abc 46";
	answers = str_to_target_ids(question, strnlen(question, 128),
			&nr_integers);
	KUNIT_EXPECT_EQ(test, (ssize_t)2, nr_integers);
	for (i = 0; i < 2; i++)
		KUNIT_EXPECT_EQ(test, expected[i], answers[i]);
	kfree(answers);

	question = "";
	answers = str_to_target_ids(question, strnlen(question, 128),
			&nr_integers);
	KUNIT_EXPECT_EQ(test, (ssize_t)0, nr_integers);
	kfree(answers);

	question = "\n";
	answers = str_to_target_ids(question, strnlen(question, 128),
			&nr_integers);
	KUNIT_EXPECT_EQ(test, (ssize_t)0, nr_integers);
	kfree(answers);
}

static void damon_dbgfs_test_set_targets(struct kunit *test)
{
	struct damon_ctx *ctx = dbgfs_new_ctx();
	unsigned long ids[] = {1, 2, 3};
	char buf[64];

	/* Make DAMON consider target id as plain number */
	ctx->primitive.target_valid = NULL;
	ctx->primitive.cleanup = NULL;

	damon_set_targets(ctx, ids, 3);
	sprint_target_ids(ctx, buf, 64);
	KUNIT_EXPECT_STREQ(test, (char *)buf, "1 2 3\n");

	damon_set_targets(ctx, NULL, 0);
	sprint_target_ids(ctx, buf, 64);
	KUNIT_EXPECT_STREQ(test, (char *)buf, "\n");

	damon_set_targets(ctx, (unsigned long []){1, 2}, 2);
	sprint_target_ids(ctx, buf, 64);
	KUNIT_EXPECT_STREQ(test, (char *)buf, "1 2\n");

	damon_set_targets(ctx, (unsigned long []){2}, 1);
	sprint_target_ids(ctx, buf, 64);
	KUNIT_EXPECT_STREQ(test, (char *)buf, "2\n");

	damon_set_targets(ctx, NULL, 0);
	sprint_target_ids(ctx, buf, 64);
	KUNIT_EXPECT_STREQ(test, (char *)buf, "\n");

	dbgfs_destroy_ctx(ctx);
}

static void damon_dbgfs_test_set_recording(struct kunit *test)
{
	struct damon_ctx *ctx = dbgfs_new_ctx();
	struct dbgfs_recorder *rec = ctx->callback.private;
	int err;

	err = dbgfs_set_recording(ctx, 42, "foo");
	KUNIT_EXPECT_EQ(test, err, -EINVAL);
	dbgfs_set_recording(ctx, 4242, "foo.bar");
	KUNIT_EXPECT_EQ(test, rec->rbuf_len, 4242u);
	KUNIT_EXPECT_STREQ(test, rec->rfile_path, "foo.bar");
	dbgfs_set_recording(ctx, 424242, "foo");
	KUNIT_EXPECT_EQ(test, rec->rbuf_len, 424242u);
	KUNIT_EXPECT_STREQ(test, rec->rfile_path, "foo");

	dbgfs_destroy_ctx(ctx);
}

static void damon_dbgfs_test_write_rbuf(struct kunit *test)
{
	struct damon_ctx *ctx = dbgfs_new_ctx();
	struct dbgfs_recorder *rec = ctx->callback.private;
	char *data;

	dbgfs_set_recording(ctx, 4242, "damon.data");

	data = "hello";
	dbgfs_write_rbuf(ctx, data, strnlen(data, 256));
	KUNIT_EXPECT_EQ(test, rec->rbuf_offset, 5u);

	dbgfs_write_rbuf(ctx, data, 0);
	KUNIT_EXPECT_EQ(test, rec->rbuf_offset, 5u);

	KUNIT_EXPECT_STREQ(test, (char *)rec->rbuf, data);

	dbgfs_destroy_ctx(ctx);
}

/*
 * Test dbgfs_after_aggregation()
 *
 * dbgfs sets dbgfs_after_aggregation() as aggregate callback.  It stores the
 * aggregated monitoring information ('->nr_accesses' of each regions) to the
 * result buffer.
 */
static void damon_dbgfs_test_aggregate(struct kunit *test)
{
	struct damon_ctx *ctx = dbgfs_new_ctx();
	struct dbgfs_recorder *rec = ctx->callback.private;
	unsigned long target_ids[] = {1, 2, 3};
	unsigned long saddr[][3] = {{10, 20, 30}, {5, 42, 49}, {13, 33, 55} };
	unsigned long eaddr[][3] = {{15, 27, 40}, {31, 45, 55}, {23, 44, 66} };
	unsigned long accesses[][3] = {{42, 95, 84}, {10, 20, 30}, {0, 1, 2} };
	struct damon_target *t;
	struct damon_region *r;
	int it, ir;
	ssize_t sz, sr, sp;

	/* Make DAMON consider target id as plain number */
	ctx->primitive.target_valid = NULL;
	ctx->primitive.cleanup = NULL;

	dbgfs_set_recording(ctx, 4242, "damon.data");
	damon_set_targets(ctx, target_ids, 3);

	it = 0;
	damon_for_each_target(t, ctx) {
		for (ir = 0; ir < 3; ir++) {
			r = damon_new_region(saddr[it][ir], eaddr[it][ir]);
			r->nr_accesses = accesses[it][ir];
			damon_add_region(r, t);
		}
		it++;
	}
	dbgfs_after_aggregation(ctx);

	/* The aggregated information should be written in the buffer */
	sr = sizeof(r->ar.start) + sizeof(r->ar.end) + sizeof(r->nr_accesses);
	sp = sizeof(t->id) + sizeof(unsigned int) + 3 * sr;
	sz = sizeof(struct timespec64) + sizeof(unsigned int) + 3 * sp;
	KUNIT_EXPECT_EQ(test, (unsigned int)sz, rec->rbuf_offset);

	damon_destroy_ctx(ctx);
}


static void damon_dbgfs_test_set_init_regions(struct kunit *test)
{
	struct damon_ctx *ctx = damon_new_ctx(DAMON_ADAPTIVE_TARGET);
	unsigned long ids[] = {1, 2, 3};
	/* Each line represents one region in ``<target id> <start> <end>`` */
	char * const valid_inputs[] = {"2 10 20\n 2   20 30\n2 35 45",
		"2 10 20\n",
		"2 10 20\n1 39 59\n1 70 134\n  2  20 25\n",
		""};
	/* Reading the file again will show sorted, clean output */
	char * const valid_expects[] = {"2 10 20\n2 20 30\n2 35 45\n",
		"2 10 20\n",
		"1 39 59\n1 70 134\n2 10 20\n2 20 25\n",
		""};
	char * const invalid_inputs[] = {"4 10 20\n",	/* target not exists */
		"2 10 20\n 2 14 26\n",		/* regions overlap */
		"1 10 20\n2 30 40\n 1 5 8"};	/* not sorted by address */
	char *input, *expect;
	int i, rc;
	char buf[256];

	damon_set_targets(ctx, ids, 3);

	/* Put valid inputs and check the results */
	for (i = 0; i < ARRAY_SIZE(valid_inputs); i++) {
		input = valid_inputs[i];
		expect = valid_expects[i];

		rc = set_init_regions(ctx, input, strnlen(input, 256));
		KUNIT_EXPECT_EQ(test, rc, 0);

		memset(buf, 0, 256);
		sprint_init_regions(ctx, buf, 256);

		KUNIT_EXPECT_STREQ(test, (char *)buf, expect);
	}
	/* Put invlid inputs and check the return error code */
	for (i = 0; i < ARRAY_SIZE(invalid_inputs); i++) {
		input = invalid_inputs[i];
		pr_info("input: %s\n", input);
		rc = set_init_regions(ctx, input, strnlen(input, 256));
		KUNIT_EXPECT_EQ(test, rc, -EINVAL);

		memset(buf, 0, 256);
		sprint_init_regions(ctx, buf, 256);

		KUNIT_EXPECT_STREQ(test, (char *)buf, "");
	}

	damon_set_targets(ctx, NULL, 0);
	damon_destroy_ctx(ctx);
}

static struct kunit_case damon_test_cases[] = {
	KUNIT_CASE(damon_dbgfs_test_str_to_target_ids),
	KUNIT_CASE(damon_dbgfs_test_set_targets),
	KUNIT_CASE(damon_dbgfs_test_set_recording),
	KUNIT_CASE(damon_dbgfs_test_write_rbuf),
	KUNIT_CASE(damon_dbgfs_test_aggregate),
	KUNIT_CASE(damon_dbgfs_test_set_init_regions),
	{},
};

static struct kunit_suite damon_test_suite = {
	.name = "damon-dbgfs",
	.test_cases = damon_test_cases,
};
kunit_test_suite(damon_test_suite);

#endif /* _DAMON_TEST_H */

#endif	/* CONFIG_DAMON_KUNIT_TEST */
