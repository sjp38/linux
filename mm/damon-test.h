/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Data Access Monitor Unit Tests
 *
 * Copyright 2019 Amazon.com, Inc. or its affiliates.  All rights reserved.
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 */

#ifdef CONFIG_DAMON_KUNIT_TEST

#ifndef _DAMON_TEST_H
#define _DAMON_TEST_H

#include <kunit/test.h>

static void damon_test_str_to_target_ids(struct kunit *test)
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

static void damon_test_regions(struct kunit *test)
{
	struct damon_region *r;
	struct damon_target *t;

	r = damon_new_region(1, 2);
	KUNIT_EXPECT_EQ(test, 1ul, r->ar.start);
	KUNIT_EXPECT_EQ(test, 2ul, r->ar.end);
	KUNIT_EXPECT_EQ(test, 0u, r->nr_accesses);

	t = damon_new_target(42);
	KUNIT_EXPECT_EQ(test, 0u, nr_damon_regions(t));

	damon_add_region(r, t);
	KUNIT_EXPECT_EQ(test, 1u, nr_damon_regions(t));

	damon_del_region(r);
	KUNIT_EXPECT_EQ(test, 0u, nr_damon_regions(t));

	damon_free_target(t);
}

static void damon_test_target(struct kunit *test)
{
	struct damon_ctx *c = &damon_user_ctx;
	struct damon_target *t;

	t = damon_new_target(42);
	KUNIT_EXPECT_EQ(test, 42ul, t->id);
	KUNIT_EXPECT_EQ(test, 0u, nr_damon_targets(c));

	damon_add_target(&damon_user_ctx, t);
	KUNIT_EXPECT_EQ(test, 1u, nr_damon_targets(c));

	damon_destroy_target(t);
	KUNIT_EXPECT_EQ(test, 0u, nr_damon_targets(c));
}

static void damon_test_set_targets(struct kunit *test)
{
	struct damon_ctx *ctx = &damon_user_ctx;
	unsigned long ids[] = {1, 2, 3};
	char buf[64];

	/* Make DAMON consider target id as plain number */
	ctx->target_valid = NULL;

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
}

static void damon_test_set_recording(struct kunit *test)
{
	struct damon_ctx *ctx = &damon_user_ctx;
	int err;

	err = damon_set_recording(ctx, 42, "foo");
	KUNIT_EXPECT_EQ(test, err, -EINVAL);
	damon_set_recording(ctx, 4242, "foo.bar");
	KUNIT_EXPECT_EQ(test, ctx->rbuf_len, 4242u);
	KUNIT_EXPECT_STREQ(test, ctx->rfile_path, "foo.bar");
	damon_set_recording(ctx, 424242, "foo");
	KUNIT_EXPECT_EQ(test, ctx->rbuf_len, 424242u);
	KUNIT_EXPECT_STREQ(test, ctx->rfile_path, "foo");
}

static void __link_vmas(struct vm_area_struct *vmas, ssize_t nr_vmas)
{
	int i, j;
	unsigned long largest_gap, gap;

	if (!nr_vmas)
		return;

	for (i = 0; i < nr_vmas - 1; i++) {
		vmas[i].vm_next = &vmas[i + 1];

		vmas[i].vm_rb.rb_left = NULL;
		vmas[i].vm_rb.rb_right = &vmas[i + 1].vm_rb;

		largest_gap = 0;
		for (j = i; j < nr_vmas; j++) {
			if (j == 0)
				continue;
			gap = vmas[j].vm_start - vmas[j - 1].vm_end;
			if (gap > largest_gap)
				largest_gap = gap;
		}
		vmas[i].rb_subtree_gap = largest_gap;
	}
	vmas[i].vm_next = NULL;
	vmas[i].vm_rb.rb_right = NULL;
	vmas[i].rb_subtree_gap = 0;
}

/*
 * Test damon_three_regions_in_vmas() function
 *
 * In case of virtual memory address spaces monitoring, DAMON converts the
 * complex and dynamic memory mappings of each target task to three
 * discontiguous regions which cover every mapped areas.  However, the three
 * regions should not include the two biggest unmapped areas in the original
 * mapping, because the two biggest areas are normally the areas between 1)
 * heap and the mmap()-ed regions, and 2) the mmap()-ed regions and stack.
 * Because these two unmapped areas are very huge but obviously never accessed,
 * covering the region is just a waste.
 *
 * 'damon_three_regions_in_vmas() receives an address space of a process.  It
 * first identifies the start of mappings, end of mappings, and the two biggest
 * unmapped areas.  After that, based on the information, it constructs the
 * three regions and returns.  For more detail, refer to the comment of
 * 'damon_init_regions_of()' function definition in 'mm/damon.c' file.
 *
 * For example, suppose virtual address ranges of 10-20, 20-25, 200-210,
 * 210-220, 300-305, and 307-330 (Other comments represent this mappings in
 * more short form: 10-20-25, 200-210-220, 300-305, 307-330) of a process are
 * mapped.  To cover every mappings, the three regions should start with 10,
 * and end with 305.  The process also has three unmapped areas, 25-200,
 * 220-300, and 305-307.  Among those, 25-200 and 220-300 are the biggest two
 * unmapped areas, and thus it should be converted to three regions of 10-25,
 * 200-220, and 300-330.
 */
static void damon_test_three_regions_in_vmas(struct kunit *test)
{
	struct damon_addr_range regions[3] = {0,};
	/* 10-20-25, 200-210-220, 300-305, 307-330 */
	struct vm_area_struct vmas[] = {
		(struct vm_area_struct) {.vm_start = 10, .vm_end = 20},
		(struct vm_area_struct) {.vm_start = 20, .vm_end = 25},
		(struct vm_area_struct) {.vm_start = 200, .vm_end = 210},
		(struct vm_area_struct) {.vm_start = 210, .vm_end = 220},
		(struct vm_area_struct) {.vm_start = 300, .vm_end = 305},
		(struct vm_area_struct) {.vm_start = 307, .vm_end = 330},
	};

	__link_vmas(vmas, 6);

	damon_three_regions_in_vmas(&vmas[0], regions);

	KUNIT_EXPECT_EQ(test, 10ul, regions[0].start);
	KUNIT_EXPECT_EQ(test, 25ul, regions[0].end);
	KUNIT_EXPECT_EQ(test, 200ul, regions[1].start);
	KUNIT_EXPECT_EQ(test, 220ul, regions[1].end);
	KUNIT_EXPECT_EQ(test, 300ul, regions[2].start);
	KUNIT_EXPECT_EQ(test, 330ul, regions[2].end);
}

/* Clean up global state of damon */
static void damon_cleanup_global_state(void)
{
	struct damon_target *t, *next;

	damon_for_each_target_safe(t, next, &damon_user_ctx)
		damon_destroy_target(t);

	damon_user_ctx.rbuf_offset = 0;
}

/*
 * Test kdamond_reset_aggregated()
 *
 * DAMON checks access to each region and aggregates this information as the
 * access frequency of each region.  In detail, it increases '->nr_accesses' of
 * regions that an access has confirmed.  'kdamond_reset_aggregated()' flushes
 * the aggregated information ('->nr_accesses' of each regions) to the result
 * buffer.  As a result of the flushing, the '->nr_accesses' of regions are
 * initialized to zero.
 */
static void damon_test_aggregate(struct kunit *test)
{
	struct damon_ctx *ctx = &damon_user_ctx;
	unsigned long target_ids[] = {1, 2, 3};
	unsigned long saddr[][3] = {{10, 20, 30}, {5, 42, 49}, {13, 33, 55} };
	unsigned long eaddr[][3] = {{15, 27, 40}, {31, 45, 55}, {23, 44, 66} };
	unsigned long accesses[][3] = {{42, 95, 84}, {10, 20, 30}, {0, 1, 2} };
	struct damon_target *t;
	struct damon_region *r;
	int it, ir;
	ssize_t sz, sr, sp;

	damon_set_recording(ctx, 4242, "damon.data");
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
	kdamond_reset_aggregated(ctx);
	it = 0;
	damon_for_each_target(t, ctx) {
		ir = 0;
		/* '->nr_accesses' should be zeroed */
		damon_for_each_region(r, t) {
			KUNIT_EXPECT_EQ(test, 0u, r->nr_accesses);
			ir++;
		}
		/* regions should be preserved */
		KUNIT_EXPECT_EQ(test, 3, ir);
		it++;
	}
	/* targets also should be preserved */
	KUNIT_EXPECT_EQ(test, 3, it);

	/* The aggregated information should be written in the buffer */
	sr = sizeof(r->ar.start) + sizeof(r->ar.end) + sizeof(r->nr_accesses);
	sp = sizeof(t->id) + sizeof(unsigned int) + 3 * sr;
	sz = sizeof(struct timespec64) + sizeof(unsigned int) + 3 * sp;
	KUNIT_EXPECT_EQ(test, (unsigned int)sz, ctx->rbuf_offset);

	damon_set_recording(ctx, 0, "damon.data");
	damon_cleanup_global_state();
}

static void damon_test_write_rbuf(struct kunit *test)
{
	struct damon_ctx *ctx = &damon_user_ctx;
	char *data;

	damon_set_recording(&damon_user_ctx, 4242, "damon.data");

	data = "hello";
	damon_write_rbuf(ctx, data, strnlen(data, 256));
	KUNIT_EXPECT_EQ(test, ctx->rbuf_offset, 5u);

	damon_write_rbuf(ctx, data, 0);
	KUNIT_EXPECT_EQ(test, ctx->rbuf_offset, 5u);

	KUNIT_EXPECT_STREQ(test, (char *)ctx->rbuf, data);
	damon_set_recording(&damon_user_ctx, 0, "damon.data");
}

static struct damon_region *__nth_region_of(struct damon_target *t, int idx)
{
	struct damon_region *r;
	unsigned int i = 0;

	damon_for_each_region(r, t) {
		if (i++ == idx)
			return r;
	}

	return NULL;
}

/*
 * Test 'damon_apply_three_regions()'
 *
 * test			kunit object
 * regions		an array containing start/end addresses of current
 *			monitoring target regions
 * nr_regions		the number of the addresses in 'regions'
 * three_regions	The three regions that need to be applied now
 * expected		start/end addresses of monitoring target regions that
 *			'three_regions' are applied
 * nr_expected		the number of addresses in 'expected'
 *
 * The memory mapping of the target processes changes dynamically.  To follow
 * the change, DAMON periodically reads the mappings, simplifies it to the
 * three regions, and updates the monitoring target regions to fit in the three
 * regions.  The update of current target regions is the role of
 * 'damon_apply_three_regions()'.
 *
 * This test passes the given target regions and the new three regions that
 * need to be applied to the function and check whether it updates the regions
 * as expected.
 */
static void damon_do_test_apply_three_regions(struct kunit *test,
				unsigned long *regions, int nr_regions,
				struct damon_addr_range *three_regions,
				unsigned long *expected, int nr_expected)
{
	struct damon_target *t;
	struct damon_region *r;
	int i;

	t = damon_new_target(42);
	for (i = 0; i < nr_regions / 2; i++) {
		r = damon_new_region(regions[i * 2], regions[i * 2 + 1]);
		damon_add_region(r, t);
	}
	damon_add_target(&damon_user_ctx, t);

	damon_apply_three_regions(&damon_user_ctx, t, three_regions);

	for (i = 0; i < nr_expected / 2; i++) {
		r = __nth_region_of(t, i);
		KUNIT_EXPECT_EQ(test, r->ar.start, expected[i * 2]);
		KUNIT_EXPECT_EQ(test, r->ar.end, expected[i * 2 + 1]);
	}

	damon_cleanup_global_state();
}

/*
 * This function test most common case where the three big regions are only
 * slightly changed.  Target regions should adjust their boundary (10-20-30,
 * 50-55, 70-80, 90-100) to fit with the new big regions or remove target
 * regions (57-79) that now out of the three regions.
 */
static void damon_test_apply_three_regions1(struct kunit *test)
{
	/* 10-20-30, 50-55-57-59, 70-80-90-100 */
	unsigned long regions[] = {10, 20, 20, 30, 50, 55, 55, 57, 57, 59,
				70, 80, 80, 90, 90, 100};
	/* 5-27, 45-55, 73-104 */
	struct damon_addr_range new_three_regions[3] = {
		(struct damon_addr_range){.start = 5, .end = 27},
		(struct damon_addr_range){.start = 45, .end = 55},
		(struct damon_addr_range){.start = 73, .end = 104} };
	/* 5-20-27, 45-55, 73-80-90-104 */
	unsigned long expected[] = {5, 20, 20, 27, 45, 55,
				73, 80, 80, 90, 90, 104};

	damon_do_test_apply_three_regions(test, regions, ARRAY_SIZE(regions),
			new_three_regions, expected, ARRAY_SIZE(expected));
}

/*
 * Test slightly bigger change.  Similar to above, but the second big region
 * now require two target regions (50-55, 57-59) to be removed.
 */
static void damon_test_apply_three_regions2(struct kunit *test)
{
	/* 10-20-30, 50-55-57-59, 70-80-90-100 */
	unsigned long regions[] = {10, 20, 20, 30, 50, 55, 55, 57, 57, 59,
				70, 80, 80, 90, 90, 100};
	/* 5-27, 56-57, 65-104 */
	struct damon_addr_range new_three_regions[3] = {
		(struct damon_addr_range){.start = 5, .end = 27},
		(struct damon_addr_range){.start = 56, .end = 57},
		(struct damon_addr_range){.start = 65, .end = 104} };
	/* 5-20-27, 56-57, 65-80-90-104 */
	unsigned long expected[] = {5, 20, 20, 27, 56, 57,
				65, 80, 80, 90, 90, 104};

	damon_do_test_apply_three_regions(test, regions, ARRAY_SIZE(regions),
			new_three_regions, expected, ARRAY_SIZE(expected));
}

/*
 * Test a big change.  The second big region has totally freed and mapped to
 * different area (50-59 -> 61-63).  The target regions which were in the old
 * second big region (50-55-57-59) should be removed and new target region
 * covering the second big region (61-63) should be created.
 */
static void damon_test_apply_three_regions3(struct kunit *test)
{
	/* 10-20-30, 50-55-57-59, 70-80-90-100 */
	unsigned long regions[] = {10, 20, 20, 30, 50, 55, 55, 57, 57, 59,
				70, 80, 80, 90, 90, 100};
	/* 5-27, 61-63, 65-104 */
	struct damon_addr_range new_three_regions[3] = {
		(struct damon_addr_range){.start = 5, .end = 27},
		(struct damon_addr_range){.start = 61, .end = 63},
		(struct damon_addr_range){.start = 65, .end = 104} };
	/* 5-20-27, 61-63, 65-80-90-104 */
	unsigned long expected[] = {5, 20, 20, 27, 61, 63,
				65, 80, 80, 90, 90, 104};

	damon_do_test_apply_three_regions(test, regions, ARRAY_SIZE(regions),
			new_three_regions, expected, ARRAY_SIZE(expected));
}

/*
 * Test another big change.  Both of the second and third big regions (50-59
 * and 70-100) has totally freed and mapped to different area (30-32 and
 * 65-68).  The target regions which were in the old second and third big
 * regions should now be removed and new target regions covering the new second
 * and third big regions should be crated.
 */
static void damon_test_apply_three_regions4(struct kunit *test)
{
	/* 10-20-30, 50-55-57-59, 70-80-90-100 */
	unsigned long regions[] = {10, 20, 20, 30, 50, 55, 55, 57, 57, 59,
				70, 80, 80, 90, 90, 100};
	/* 5-7, 30-32, 65-68 */
	struct damon_addr_range new_three_regions[3] = {
		(struct damon_addr_range){.start = 5, .end = 7},
		(struct damon_addr_range){.start = 30, .end = 32},
		(struct damon_addr_range){.start = 65, .end = 68} };
	/* expect 5-7, 30-32, 65-68 */
	unsigned long expected[] = {5, 7, 30, 32, 65, 68};

	damon_do_test_apply_three_regions(test, regions, ARRAY_SIZE(regions),
			new_three_regions, expected, ARRAY_SIZE(expected));
}

static void damon_test_split_evenly(struct kunit *test)
{
	struct damon_ctx *c = &damon_user_ctx;
	struct damon_target *t;
	struct damon_region *r;
	unsigned long i;

	KUNIT_EXPECT_EQ(test, damon_split_region_evenly(c, NULL, 5), -EINVAL);

	t = damon_new_target(42);
	r = damon_new_region(0, 100);
	KUNIT_EXPECT_EQ(test, damon_split_region_evenly(c, r, 0), -EINVAL);

	damon_add_region(r, t);
	KUNIT_EXPECT_EQ(test, damon_split_region_evenly(c, r, 10), 0);
	KUNIT_EXPECT_EQ(test, nr_damon_regions(t), 10u);

	i = 0;
	damon_for_each_region(r, t) {
		KUNIT_EXPECT_EQ(test, r->ar.start, i++ * 10);
		KUNIT_EXPECT_EQ(test, r->ar.end, i * 10);
	}
	damon_free_target(t);

	t = damon_new_target(42);
	r = damon_new_region(5, 59);
	damon_add_region(r, t);
	KUNIT_EXPECT_EQ(test, damon_split_region_evenly(c, r, 5), 0);
	KUNIT_EXPECT_EQ(test, nr_damon_regions(t), 5u);

	i = 0;
	damon_for_each_region(r, t) {
		if (i == 4)
			break;
		KUNIT_EXPECT_EQ(test, r->ar.start, 5 + 10 * i++);
		KUNIT_EXPECT_EQ(test, r->ar.end, 5 + 10 * i);
	}
	KUNIT_EXPECT_EQ(test, r->ar.start, 5 + 10 * i);
	KUNIT_EXPECT_EQ(test, r->ar.end, 59ul);
	damon_free_target(t);

	t = damon_new_target(42);
	r = damon_new_region(5, 6);
	damon_add_region(r, t);
	KUNIT_EXPECT_EQ(test, damon_split_region_evenly(c, r, 2), -EINVAL);
	KUNIT_EXPECT_EQ(test, nr_damon_regions(t), 1u);

	damon_for_each_region(r, t) {
		KUNIT_EXPECT_EQ(test, r->ar.start, 5ul);
		KUNIT_EXPECT_EQ(test, r->ar.end, 6ul);
	}
	damon_free_target(t);
}

static void damon_test_split_at(struct kunit *test)
{
	struct damon_target *t;
	struct damon_region *r;

	t = damon_new_target(42);
	r = damon_new_region(0, 100);
	damon_add_region(r, t);
	damon_split_region_at(&damon_user_ctx, r, 25);
	KUNIT_EXPECT_EQ(test, r->ar.start, 0ul);
	KUNIT_EXPECT_EQ(test, r->ar.end, 25ul);

	r = damon_next_region(r);
	KUNIT_EXPECT_EQ(test, r->ar.start, 25ul);
	KUNIT_EXPECT_EQ(test, r->ar.end, 100ul);

	damon_free_target(t);
}

static void damon_test_merge_two(struct kunit *test)
{
	struct damon_target *t;
	struct damon_region *r, *r2, *r3;
	int i;

	t = damon_new_target(42);
	r = damon_new_region(0, 100);
	r->nr_accesses = 10;
	damon_add_region(r, t);
	r2 = damon_new_region(100, 300);
	r2->nr_accesses = 20;
	damon_add_region(r2, t);

	damon_merge_two_regions(r, r2);
	KUNIT_EXPECT_EQ(test, r->ar.start, 0ul);
	KUNIT_EXPECT_EQ(test, r->ar.end, 300ul);
	KUNIT_EXPECT_EQ(test, r->nr_accesses, 16u);

	i = 0;
	damon_for_each_region(r3, t) {
		KUNIT_EXPECT_PTR_EQ(test, r, r3);
		i++;
	}
	KUNIT_EXPECT_EQ(test, i, 1);

	damon_free_target(t);
}

static void damon_test_merge_regions_of(struct kunit *test)
{
	struct damon_target *t;
	struct damon_region *r;
	unsigned long sa[] = {0, 100, 114, 122, 130, 156, 170, 184};
	unsigned long ea[] = {100, 112, 122, 130, 156, 170, 184, 230};
	unsigned int nrs[] = {0, 0, 10, 10, 20, 30, 1, 2};

	unsigned long saddrs[] = {0, 114, 130, 156, 170};
	unsigned long eaddrs[] = {112, 130, 156, 170, 230};
	int i;

	t = damon_new_target(42);
	for (i = 0; i < ARRAY_SIZE(sa); i++) {
		r = damon_new_region(sa[i], ea[i]);
		r->nr_accesses = nrs[i];
		damon_add_region(r, t);
	}

	damon_merge_regions_of(t, 9, 9999);
	/* 0-112, 114-130, 130-156, 156-170 */
	KUNIT_EXPECT_EQ(test, nr_damon_regions(t), 5u);
	for (i = 0; i < 5; i++) {
		r = __nth_region_of(t, i);
		KUNIT_EXPECT_EQ(test, r->ar.start, saddrs[i]);
		KUNIT_EXPECT_EQ(test, r->ar.end, eaddrs[i]);
	}
	damon_free_target(t);
}

static void damon_test_split_regions_of(struct kunit *test)
{
	struct damon_target *t;
	struct damon_region *r;

	t = damon_new_target(42);
	r = damon_new_region(0, 22);
	damon_add_region(r, t);
	damon_split_regions_of(&damon_user_ctx, t, 2);
	KUNIT_EXPECT_EQ(test, nr_damon_regions(t), 2u);
	damon_free_target(t);

	t = damon_new_target(42);
	r = damon_new_region(0, 220);
	damon_add_region(r, t);
	damon_split_regions_of(&damon_user_ctx, t, 4);
	KUNIT_EXPECT_EQ(test, nr_damon_regions(t), 4u);
	damon_free_target(t);
}

static struct kunit_case damon_test_cases[] = {
	KUNIT_CASE(damon_test_str_to_target_ids),
	KUNIT_CASE(damon_test_target),
	KUNIT_CASE(damon_test_regions),
	KUNIT_CASE(damon_test_set_targets),
	KUNIT_CASE(damon_test_set_recording),
	KUNIT_CASE(damon_test_three_regions_in_vmas),
	KUNIT_CASE(damon_test_aggregate),
	KUNIT_CASE(damon_test_write_rbuf),
	KUNIT_CASE(damon_test_apply_three_regions1),
	KUNIT_CASE(damon_test_apply_three_regions2),
	KUNIT_CASE(damon_test_apply_three_regions3),
	KUNIT_CASE(damon_test_apply_three_regions4),
	KUNIT_CASE(damon_test_split_evenly),
	KUNIT_CASE(damon_test_split_at),
	KUNIT_CASE(damon_test_merge_two),
	KUNIT_CASE(damon_test_merge_regions_of),
	KUNIT_CASE(damon_test_split_regions_of),
	{},
};

static struct kunit_suite damon_test_suite = {
	.name = "damon",
	.test_cases = damon_test_cases,
};
kunit_test_suite(damon_test_suite);

#endif /* _DAMON_TEST_H */

#endif	/* CONFIG_DAMON_KUNIT_TEST */
