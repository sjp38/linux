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

static void damon_test_str_to_pids(struct kunit *test)
{
	char *question;
	unsigned long *answers;
	unsigned long expected[] = {12, 35, 46};
	ssize_t nr_integers = 0, i;

	question = "123";
	answers = str_to_pids(question, strnlen(question, 128), &nr_integers);
	KUNIT_EXPECT_EQ(test, 1l, nr_integers);
	KUNIT_EXPECT_EQ(test, 123ul, answers[0]);
	kfree(answers);

	question = "123abc";
	answers = str_to_pids(question, strnlen(question, 128), &nr_integers);
	KUNIT_EXPECT_EQ(test, 1l, nr_integers);
	KUNIT_EXPECT_EQ(test, 123ul, answers[0]);
	kfree(answers);

	question = "a123";
	answers = str_to_pids(question, strnlen(question, 128), &nr_integers);
	KUNIT_EXPECT_EQ(test, 0l, nr_integers);
	KUNIT_EXPECT_PTR_EQ(test, answers, (unsigned long *)NULL);

	question = "12 35";
	answers = str_to_pids(question, strnlen(question, 128), &nr_integers);
	KUNIT_EXPECT_EQ(test, 2l, nr_integers);
	for (i = 0; i < nr_integers; i++)
		KUNIT_EXPECT_EQ(test, expected[i], answers[i]);
	kfree(answers);

	question = "12 35 46";
	answers = str_to_pids(question, strnlen(question, 128), &nr_integers);
	KUNIT_EXPECT_EQ(test, 3l, nr_integers);
	for (i = 0; i < nr_integers; i++)
		KUNIT_EXPECT_EQ(test, expected[i], answers[i]);
	kfree(answers);

	question = "12 35 abc 46";
	answers = str_to_pids(question, strnlen(question, 128), &nr_integers);
	KUNIT_EXPECT_EQ(test, 2l, nr_integers);
	for (i = 0; i < 2; i++)
		KUNIT_EXPECT_EQ(test, expected[i], answers[i]);
	kfree(answers);

	question = "";
	answers = str_to_pids(question, strnlen(question, 128), &nr_integers);
	KUNIT_EXPECT_EQ(test, 0l, nr_integers);
	KUNIT_EXPECT_PTR_EQ(test, (unsigned long *)NULL, answers);
	kfree(answers);

	question = "\n";
	answers = str_to_pids(question, strnlen(question, 128), &nr_integers);
	KUNIT_EXPECT_EQ(test, 0l, nr_integers);
	KUNIT_EXPECT_PTR_EQ(test, (unsigned long *)NULL, answers);
	kfree(answers);
}

static void damon_test_regions(struct kunit *test)
{
	struct damon_region *r;
	struct damon_task *t;

	r = damon_new_region(1, 2);
	KUNIT_EXPECT_EQ(test, 1ul, r->vm_start);
	KUNIT_EXPECT_EQ(test, 2ul, r->vm_end);
	KUNIT_EXPECT_EQ(test, 0u, r->nr_accesses);
	KUNIT_EXPECT_TRUE(test, r->sampling_addr >= r->vm_start);
	KUNIT_EXPECT_TRUE(test, r->sampling_addr < r->vm_end);

	t = damon_new_task(42);
	KUNIT_EXPECT_EQ(test, 0u, nr_damon_regions(t));

	damon_add_region_tail(r, t);
	KUNIT_EXPECT_EQ(test, 1u, nr_damon_regions(t));

	damon_del_region(r);
	KUNIT_EXPECT_EQ(test, 0u, nr_damon_regions(t));

	damon_free_task(t);
}

static void damon_test_tasks(struct kunit *test)
{
	struct damon_task *t;

	t = damon_new_task(42);
	KUNIT_EXPECT_EQ(test, 42ul, t->pid);
	KUNIT_EXPECT_EQ(test, 0u, nr_damon_tasks());

	damon_add_task_tail(t);
	KUNIT_EXPECT_EQ(test, 1u, nr_damon_tasks());

	damon_destroy_task(t);
	KUNIT_EXPECT_EQ(test, 0u, nr_damon_tasks());
}

static void damon_test_set_pids(struct kunit *test)
{
	unsigned long pids[] = {1, 2, 3};
	char buf[64];

	damon_set_pids(pids, 3);
	damon_sprint_pids(buf, 64);
	KUNIT_EXPECT_STREQ(test, (char *)buf, "1 2 3\n");

	damon_set_pids(NULL, 0);
	damon_sprint_pids(buf, 64);
	KUNIT_EXPECT_STREQ(test, (char *)buf, "\n");

	damon_set_pids((unsigned long []){1, 2}, 2);
	damon_sprint_pids(buf, 64);
	KUNIT_EXPECT_STREQ(test, (char *)buf, "1 2\n");

	damon_set_pids((unsigned long []){2}, 1);
	damon_sprint_pids(buf, 64);
	KUNIT_EXPECT_STREQ(test, (char *)buf, "2\n");

	damon_set_pids(NULL, 0);
	damon_sprint_pids(buf, 64);
	KUNIT_EXPECT_STREQ(test, (char *)buf, "\n");
}

/*
 * Test damon_three_regions_in_vmas() function
 *
 * DAMON converts the complex and dynamic memory mappings of each target task
 * to three discontiguous regions which cover every mapped areas.  The two gaps
 * between the areas is two biggest unmapped areas in the original mapping.
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
	struct region regions[3] = {0,};
	/* 10-20-25, 200-210-220, 300-305, 307-330 */
	struct vm_area_struct vmas[] = {
		(struct vm_area_struct) {.vm_start = 10, .vm_end = 20},
		(struct vm_area_struct) {.vm_start = 20, .vm_end = 25},
		(struct vm_area_struct) {.vm_start = 200, .vm_end = 210},
		(struct vm_area_struct) {.vm_start = 210, .vm_end = 220},
		(struct vm_area_struct) {.vm_start = 300, .vm_end = 305},
		(struct vm_area_struct) {.vm_start = 307, .vm_end = 330},
	};
	vmas[0].vm_next = &vmas[1];
	vmas[1].vm_next = &vmas[2];
	vmas[2].vm_next = &vmas[3];
	vmas[3].vm_next = &vmas[4];
	vmas[4].vm_next = &vmas[5];
	vmas[5].vm_next = NULL;

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
	struct damon_task *t, *next;

	damon_for_each_task_safe(t, next)
		damon_destroy_task(t);

	damon_rbuf_offset = 0;
}

/*
 * Test kdamond_flush_aggregated()
 *
 * DAMON checks access to each region and aggregates this information as the
 * access frequency of each region.  In detail, it increases '->nr_accesses' of
 * regions that an access has confirmed.  'kdamond_flush_aggregated()' flushes
 * the aggregated information ('->nr_accesses' of each regions) to the result
 * buffer.  As a result of the flushing, the '->nr_accesses' of regions are
 * initialized to zero.
 */
static void damon_test_aggregate(struct kunit *test)
{
	unsigned long pids[] = {1, 2, 3};
	unsigned long saddr[][3] = {{10, 20, 30}, {5, 42, 49}, {13, 33, 55} };
	unsigned long eaddr[][3] = {{15, 27, 40}, {31, 45, 55}, {23, 44, 66} };
	unsigned long accesses[][3] = {{42, 95, 84}, {10, 20, 30}, {0, 1, 2} };
	struct damon_task *t;
	struct damon_region *r;
	int it, ir;
	ssize_t sz, sr, sp;

	damon_set_pids(pids, 3);

	it = 0;
	damon_for_each_task(t) {
		for (ir = 0; ir < 3; ir++) {
			r = damon_new_region(saddr[it][ir], eaddr[it][ir]);
			r->nr_accesses = accesses[it][ir];
			damon_add_region_tail(r, t);
		}
		it++;
	}
	kdamond_flush_aggregated();
	it = 0;
	damon_for_each_task(t) {
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
	/* tasks also should be preserved */
	KUNIT_EXPECT_EQ(test, 3, it);

	/* The aggregated information should be written in the buffer */
	sr = sizeof(r->vm_start) + sizeof(r->vm_end) + sizeof(r->nr_accesses);
	sp = sizeof(t->pid) + sizeof(unsigned int) + 3 * sr;
	sz = sizeof(struct timespec64) + sizeof(unsigned int) + 3 * sp;
	KUNIT_EXPECT_EQ(test, (unsigned int)sz, damon_rbuf_offset);

	damon_cleanup_global_state();
}

static void damon_test_write_rbuf(struct kunit *test)
{
	char *data;

	data = "hello";
	damon_write_rbuf(data, strnlen(data, 256));
	KUNIT_EXPECT_EQ(test, damon_rbuf_offset, 5u);

	damon_write_rbuf(data, 0);
	KUNIT_EXPECT_EQ(test, damon_rbuf_offset, 5u);

	KUNIT_EXPECT_STREQ(test, (char *)damon_rbuf, data);
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
				struct region *three_regions,
				unsigned long *expected, int nr_expected)
{
	struct damon_task *t;
	struct damon_region *r;
	int i;

	t = damon_new_task(42);
	for (i = 0; i < nr_regions / 2; i++) {
		r = damon_new_region(regions[i * 2], regions[i * 2 + 1]);
		damon_add_region_tail(r, t);
	}
	damon_add_task_tail(t);

	damon_apply_three_regions(t, three_regions);

	for (i = 0; i < nr_expected / 2; i++) {
		r = damon_nth_region_of(t, i);
		KUNIT_EXPECT_EQ(test, r->vm_start, expected[i * 2]);
		KUNIT_EXPECT_EQ(test, r->vm_end, expected[i * 2 + 1]);
	}

	damon_cleanup_global_state();
}

/* below 4 functions test differnt inputs for 'damon_apply_three_regions()' */
static void damon_test_apply_three_regions1(struct kunit *test)
{
	/* 10-20-30, 50-55-57-59, 70-80-90-100 */
	unsigned long regions[] = {10, 20, 20, 30, 50, 55, 55, 57, 57, 59,
				70, 80, 80, 90, 90, 100};
	/* 5-27, 45-55, 73-104 */
	struct region new_three_regions[3] = {
		(struct region){.start = 5, .end = 27},
		(struct region){.start = 45, .end = 55},
		(struct region){.start = 73, .end = 104} };
	/* 5-20-27, 45-55, 73-80-90-104 */
	unsigned long expected[] = {5, 20, 20, 27, 45, 55,
				73, 80, 80, 90, 90, 104};

	damon_do_test_apply_three_regions(test, regions, ARRAY_SIZE(regions),
			new_three_regions, expected, ARRAY_SIZE(expected));
}

static void damon_test_apply_three_regions2(struct kunit *test)
{
	/* 10-20-30, 50-55-57-59, 70-80-90-100 */
	unsigned long regions[] = {10, 20, 20, 30, 50, 55, 55, 57, 57, 59,
				70, 80, 80, 90, 90, 100};
	/* 5-27, 56-57, 65-104 */
	struct region new_three_regions[3] = {
		(struct region){.start = 5, .end = 27},
		(struct region){.start = 56, .end = 57},
		(struct region){.start = 65, .end = 104} };
	/* 5-20-27, 56-57, 65-80-90-104 */
	unsigned long expected[] = {5, 20, 20, 27, 56, 57,
				65, 80, 80, 90, 90, 104};

	damon_do_test_apply_three_regions(test, regions, ARRAY_SIZE(regions),
			new_three_regions, expected, ARRAY_SIZE(expected));
}

static void damon_test_apply_three_regions3(struct kunit *test)
{
	/* 10-20-30, 50-55-57-59, 70-80-90-100 */
	unsigned long regions[] = {10, 20, 20, 30, 50, 55, 55, 57, 57, 59,
				70, 80, 80, 90, 90, 100};
	/* 5-27, 61-63, 65-104 */
	struct region new_three_regions[3] = {
		(struct region){.start = 5, .end = 27},
		(struct region){.start = 61, .end = 63},
		(struct region){.start = 65, .end = 104} };
	/* 5-20-27, 61-63, 65-80-90-104 */
	unsigned long expected[] = {5, 20, 20, 27, 61, 63,
				65, 80, 80, 90, 90, 104};

	damon_do_test_apply_three_regions(test, regions, ARRAY_SIZE(regions),
			new_three_regions, expected, ARRAY_SIZE(expected));
}

static void damon_test_apply_three_regions4(struct kunit *test)
{
	/* 10-20-30, 50-55-57-59, 70-80-90-100 */
	unsigned long regions[] = {10, 20, 20, 30, 50, 55, 55, 57, 57, 59,
				70, 80, 80, 90, 90, 100};
	/* 5-7, 30-32, 65-68 */
	struct region new_three_regions[3] = {
		(struct region){.start = 5, .end = 7},
		(struct region){.start = 30, .end = 32},
		(struct region){.start = 65, .end = 68} };
	/* expect 5-7, 30-32, 65-68 */
	unsigned long expected[] = {5, 7, 30, 32, 65, 68};

	damon_do_test_apply_three_regions(test, regions, ARRAY_SIZE(regions),
			new_three_regions, expected, ARRAY_SIZE(expected));
}

static void damon_test_split_evenly(struct kunit *test)
{
	struct damon_task *t;
	struct damon_region *r;
	unsigned long i;

	KUNIT_EXPECT_EQ(test, damon_split_region_evenly(NULL, 5), -EINVAL);

	t = damon_new_task(42);
	r = damon_new_region(0, 100);
	KUNIT_EXPECT_EQ(test, damon_split_region_evenly(r, 0), -EINVAL);

	damon_add_region_tail(r, t);
	KUNIT_EXPECT_EQ(test, damon_split_region_evenly(r, 10), 0);
	KUNIT_EXPECT_EQ(test, nr_damon_regions(t), 10u);

	i = 0;
	damon_for_each_region(r, t) {
		KUNIT_EXPECT_EQ(test, r->vm_start, i++ * 10);
		KUNIT_EXPECT_EQ(test, r->vm_end, i * 10);
	}
	damon_free_task(t);

	t = damon_new_task(42);
	r = damon_new_region(5, 59);
	damon_add_region_tail(r, t);
	KUNIT_EXPECT_EQ(test, damon_split_region_evenly(r, 5), 0);
	KUNIT_EXPECT_EQ(test, nr_damon_regions(t), 5u);

	i = 0;
	damon_for_each_region(r, t) {
		if (i == 4)
			break;
		KUNIT_EXPECT_EQ(test, r->vm_start, 5 + 10 * i++);
		KUNIT_EXPECT_EQ(test, r->vm_end, 5 + 10 * i);
	}
	KUNIT_EXPECT_EQ(test, r->vm_start, 5 + 10 * i);
	KUNIT_EXPECT_EQ(test, r->vm_end, 59ul);
	damon_free_task(t);

	t = damon_new_task(42);
	r = damon_new_region(5, 6);
	damon_add_region_tail(r, t);
	KUNIT_EXPECT_EQ(test, damon_split_region_evenly(r, 2), -EINVAL);
	KUNIT_EXPECT_EQ(test, nr_damon_regions(t), 1u);

	damon_for_each_region(r, t) {
		KUNIT_EXPECT_EQ(test, r->vm_start, 5ul);
		KUNIT_EXPECT_EQ(test, r->vm_end, 6ul);
	}
	damon_free_task(t);
}

static void damon_test_split_at(struct kunit *test)
{
	struct damon_task *t;
	struct damon_region *r;

	t = damon_new_task(42);
	r = damon_new_region(0, 100);
	damon_add_region_tail(r, t);
	damon_split_region_at(r, 25);
	KUNIT_EXPECT_EQ(test, r->vm_start, 0ul);
	KUNIT_EXPECT_EQ(test, r->vm_end, 25ul);

	r = damon_next_region(r);
	KUNIT_EXPECT_EQ(test, r->vm_start, 25ul);
	KUNIT_EXPECT_EQ(test, r->vm_end, 100ul);

	damon_free_task(t);
}

static void damon_test_merge_two(struct kunit *test)
{
	struct damon_task *t;
	struct damon_region *r, *r2, *r3;
	int i;

	t = damon_new_task(42);
	r = damon_new_region(0, 100);
	r->nr_accesses = 10;
	damon_add_region_tail(r, t);
	r2 = damon_new_region(100, 300);
	r2->nr_accesses = 20;
	damon_add_region_tail(r2, t);

	damon_merge_two_regions(r, r2);
	KUNIT_EXPECT_EQ(test, r->vm_start, 0ul);
	KUNIT_EXPECT_EQ(test, r->vm_end, 300ul);
	KUNIT_EXPECT_EQ(test, r->nr_accesses, 16u);

	i = 0;
	damon_for_each_region(r3, t) {
		KUNIT_EXPECT_PTR_EQ(test, r, r3);
		i++;
	}
	KUNIT_EXPECT_EQ(test, i, 1);

	damon_free_task(t);
}

static void damon_test_merge_regions_of(struct kunit *test)
{
	struct damon_task *t;
	struct damon_region *r;
	unsigned long sa[] = {0, 100, 114, 122, 130, 156, 170, 184};
	unsigned long ea[] = {100, 112, 122, 130, 156, 170, 184, 230};
	unsigned int nrs[] = {0, 0, 10, 10, 20, 30, 1, 2};

	unsigned long saddrs[] = {0, 114, 130, 156, 170};
	unsigned long eaddrs[] = {112, 130, 156, 170, 230};
	int i;

	t = damon_new_task(42);
	for (i = 0; i < ARRAY_SIZE(sa); i++) {
		r = damon_new_region(sa[i], ea[i]);
		r->nr_accesses = nrs[i];
		damon_add_region_tail(r, t);
	}

	damon_merge_regions_of(t, 9);
	/* 0-112, 114-130, 130-156, 156-170 */
	KUNIT_EXPECT_EQ(test, nr_damon_regions(t), 5u);
	for (i = 0; i < 5; i++) {
		r = damon_nth_region_of(t, i);
		KUNIT_EXPECT_EQ(test, r->vm_start, saddrs[i]);
		KUNIT_EXPECT_EQ(test, r->vm_end, eaddrs[i]);
	}
	damon_free_task(t);
}

static void damon_test_split_regions_of(struct kunit *test)
{
	struct damon_task *t;
	struct damon_region *r;

	t = damon_new_task(42);
	r = damon_new_region(0, 22);
	damon_add_region_tail(r, t);
	damon_split_regions_of(t);
	KUNIT_EXPECT_EQ(test, nr_damon_regions(t), 2u);
	damon_free_task(t);
}

static void damon_test_kdamond_need_stop(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, kdamond_need_stop());
}

static struct kunit_case damon_test_cases[] = {
	KUNIT_CASE(damon_test_str_to_pids),
	KUNIT_CASE(damon_test_tasks),
	KUNIT_CASE(damon_test_regions),
	KUNIT_CASE(damon_test_set_pids),
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
	KUNIT_CASE(damon_test_kdamond_need_stop),
	{},
};

static struct kunit_suite damon_test_suite = {
	.name = "damon",
	.test_cases = damon_test_cases,
};
kunit_test_suite(damon_test_suite);

#endif /* _DAMON_TEST_H */

#endif	/* CONFIG_DAMON_KUNIT_TEST */
