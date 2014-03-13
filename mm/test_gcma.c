/*
 * test_gcma.c - test guaranteed contiguous memory allocator
 *
 * gcma is a contiguous memory allocator which guarantees success and
 * maximum wait time for allocation request.
 * It secure large amount of memory and let it be allocated to the
 * contiguous memory request while it can be used as backend for
 * frontswap and cleancache concurrently.
 *
 * Copyright (C) 2014   Minchan Kim <minchan@kernel.org>
 *                      SeongJae Park <sj38.park@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/gcma.h>

/*********************************
* tunables
**********************************/
/* Enable/disable gcma (disabled by default, fixed at boot for now) */
static bool enabled __read_mostly;
module_param_named(enabled, enabled, bool, 0);

static int test_gcma_alloc_release(void)
{
	struct page *cma = gcma_alloc_from_contiguous(0, 10, 1);
	if (cma == NULL)
		return -1;

	if (!gcma_release_from_contiguous(0, cma, 10))
		return -1;

	/* This should be failed because freeing free area */
	if (gcma_release_from_contiguous(0, cma, 10))
		return -1;

	return 0;
}

static int test_gcma_merge(void)
{
	struct page *cma1, *cma2, *cma3;

	pr_info("%s called\n", __func__);

	cma1 = gcma_alloc_from_contiguous(0, 10, 1);
	pr_info("[test] alloc 1st\n");
	if (cma1 == NULL)
		return -1;
	cma2 = gcma_alloc_from_contiguous(0, 20, 1);
	pr_info("[test] alloc 2nd\n");
	if (cma2 == NULL)
		return -1;
	cma3 = gcma_alloc_from_contiguous(0, 5, 1);
	pr_info("[test] alloc 3rd\n");
	if (cma3 == NULL)
		return -1;

	pr_info("[test] release 2nd\n");
	if (!gcma_release_from_contiguous(0, cma2, 20))
		return -1;
	pr_info("[test] free 1st\n");
	if (!gcma_release_from_contiguous(0, cma1, 10))
		return -1;
	pr_info("[test] free 3rd\n");
	if (!gcma_release_from_contiguous(0, cma3, 5))
		return -1;

	return 0;
}

#define do_test(test)					\
	do {						\
		if (test()) {				\
			pr_err("[FAIL] " #test "\n");	\
			return -1;			\
		}					\
		pr_info("[SUCCESS] " #test "\n");	\
	} while (0)

/*********************************
* module init and exit
**********************************/
static int __init init_gcma(void)
{
	if (!enabled)
		return 0;
	pr_info("test gcma\n");

	do_test(test_gcma_alloc_release);
	do_test(test_gcma_merge);

	return 0;
}

module_init(init_gcma);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Minchan Kim <minchan@kernel.org>");
MODULE_AUTHOR("SeongJae Park <sj38.park@gmail.com>");
MODULE_DESCRIPTION("Test for Guaranteed contiguous memory allocator");
