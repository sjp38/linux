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
#include <linux/gcma.h>
#include <linux/mm.h>
#include <linux/dma-contiguous.h>
#include <linux/cleancache.h>

/*********************************
* tunables
**********************************/
/* Enable/disable gcma (disabled by default, fixed at boot for now) */
static bool enabled __read_mostly;
module_param_named(enabled, enabled, bool, 0);

extern void gcma_frontswap_init(unsigned type);
extern int gcma_frontswap_store(unsigned type, pgoff_t offset,
		struct page *page);
extern int gcma_frontswap_load(unsigned type, pgoff_t offset,
		struct page *page);
extern void gcma_frontswap_invalidate_page(unsigned type, pgoff_t offset);
extern void gcma_frontswap_invalidate_area(unsigned type);

/**
 * returns 0 if fail
 */
static unsigned long measure_gcma(void)
{
	struct page *page;
	struct timespec start, end;
	getnstimeofday(&start);
	page = gcma_alloc_contig(0, 10);
	if (!page)
		return 0;
	getnstimeofday(&end);
	gcma_release_contig(0, page, 10);

	return end.tv_sec * 1000000000 + end.tv_nsec
		- (start.tv_sec * 1000000000 + start.tv_nsec);
}

/**
 * returns 0 if fail
 */
static unsigned long measure_cma(void)
{
	struct page *page;
	struct timespec start, end;
	getnstimeofday(&start);
	page = dma_alloc_from_contiguous(NULL, 10, 1);
	if (!page)
		return 0;
	getnstimeofday(&end);
	dma_release_from_contiguous(NULL, page, 10);

	return end.tv_sec * 1000000000 + end.tv_nsec
		- (start.tv_sec * 1000000000 + start.tv_nsec);
}

static unsigned long min_of(unsigned long prev_min, unsigned long new_val)
{
	if (prev_min == 0 || prev_min > new_val)
		return new_val;
	return prev_min;
}

static unsigned long max_of(unsigned long prev_max, unsigned long new_val)
{
	if (prev_max == 0 || prev_max < new_val)
		return new_val;
	return prev_max;
}

static int measure_time_alloc(void)
{
	unsigned long nr_gcma_fail = 0, nr_cma_fail = 0;
	unsigned long gcma_time, gcma_min = 0, gcma_max = 0, gcma_avg;
	unsigned long cma_time, cma_min = 0, cma_max = 0, cma_avg = 0;
	unsigned long gcma_sum = 0, cma_sum = 0;
	int measure_count = 500, i;

	for (i = 0; i < measure_count; i++) {
		cma_time = measure_cma();
		gcma_time = measure_gcma();

		if (!cma_time)
			nr_cma_fail++;
		if (!gcma_time)
			nr_gcma_fail++;

		gcma_min = min_of(gcma_min, gcma_time);
		gcma_max = max_of(gcma_max, gcma_time);
		gcma_sum += gcma_time;

		cma_min = min_of(cma_min, cma_time);
		cma_max = max_of(cma_max, cma_time);
		cma_sum += cma_time;
	}
	gcma_avg = gcma_sum / measure_count;
	cma_avg = cma_sum / measure_count;

	pr_info("gcma\tfail: %ld\tmin: %ld\tmax: %ld\tavg: %ld\n",
			nr_gcma_fail, gcma_min, gcma_max, gcma_avg);
	pr_info("cma\tfail: %ld\tmin: %ld\tmax: %ld\tavg: %ld\n",
			nr_cma_fail, cma_min, cma_max, cma_avg);

	return 0;
}
extern int gcma_cleancache_init_fs(size_t pagesize);
extern int gcma_cleancache_init_shared_fs(char *uuid, size_t pagesize);
extern int gcma_cleancache_get_page(int pool_id, struct cleancache_filekey key,
		pgoff_t index, struct page *page);
extern void gcma_cleancache_put_page(int pool_id,
		struct cleancache_filekey key,
		pgoff_t index, struct page *page);
extern void gcma_cleancache_invalidate_page(int pool_id,
		struct cleancache_filekey key,
		pgoff_t index);
extern void gcma_cleancache_invalidate_inode(int pool_id,
		struct cleancache_filekey key);
extern void gcma_cleancache_invalidate_fs(int pool_id);

static int test_cleancache(void)
{
	struct page *store_page, *load_page;
	void *store_page_va, *load_page_va;
	int pool_id;
	struct cleancache_filekey key;

	pool_id = gcma_cleancache_init_fs(PAGE_SIZE);
	store_page = alloc_page(GFP_KERNEL);
	if (!store_page) {
		pr_info("alloc_page failed\n");
		return -1;
	}

	store_page_va = page_address(store_page);
	memset(store_page_va, 1, PAGE_SIZE);

	memset(&key, 2, sizeof(struct cleancache_filekey));

	gcma_cleancache_put_page(pool_id, key, 17, store_page);

	load_page = alloc_page(GFP_KERNEL);
	if (!load_page) {
		pr_info("alloc_page for frontswap load op check failed\n");
		return -1;
	}
	if (gcma_cleancache_get_page(pool_id, key, 17, load_page)) {
		pr_info("failed gcma_frontswap_load call\n");
		return -1;
	}

	load_page_va = page_address(load_page);
	if (memcmp(store_page_va, load_page_va, PAGE_SIZE)) {
		pr_info("data corrupted\n");
		return -1;
	}

	gcma_cleancache_invalidate_page(pool_id, key, 17);
	if (!gcma_cleancache_get_page(pool_id, key, 17, load_page)) {
		pr_info("invalidated page still alive. test fail\n");
		return -1;
	}

	gcma_cleancache_invalidate_inode(pool_id, key);
	if (!gcma_cleancache_get_page(pool_id, key, 19, load_page)) {
		pr_info("invalidated file still alive. test fail\n");
		return -1;
	}

	gcma_cleancache_invalidate_fs(pool_id);
	memset(&key, 1, sizeof(struct cleancache_filekey));
	if (!gcma_cleancache_get_page(pool_id, key, 13, load_page)) {
		pr_info("invalidated fs still alive. test fail\n");
		return -1;
	}

	free_page((unsigned long)store_page_va);
	free_page((unsigned long)load_page_va);
	return 0;
}

static int test_frontswap(void)
{
	struct page *store_page, *load_page;
	void *store_page_va, *load_page_va;

	gcma_frontswap_init(0);
	store_page = alloc_page(GFP_KERNEL);
	if (!store_page)
		pr_info("alloc_page failed\n");

	store_page_va = page_address(store_page);
	memset(store_page_va, 1, PAGE_SIZE);
	if (gcma_frontswap_store(0, 17, store_page)) {
		pr_info("failed gcma_frontswap_store call\n");
		return -1;
	}

	load_page = alloc_page(GFP_KERNEL);
	if (!load_page) {
		pr_info("alloc_page for frontswap load op check failed\n");
		return -1;
	}
	if (gcma_frontswap_load(0, 17, load_page)) {
		pr_info("failed gcma_frontswap_load call\n");
		return -1;
	}

	load_page_va = page_address(load_page);
	if (memcmp(store_page_va, load_page_va, PAGE_SIZE)) {
		pr_info("data corrupted\n");
		return -1;
	}

	gcma_frontswap_invalidate_page(0, 17);
	if (!gcma_frontswap_load(0, 17, load_page)) {
		pr_info("invalidated page still alive. test fail\n");
		return -1;
	}

	gcma_frontswap_invalidate_area(0);
	if (!gcma_frontswap_load(0, 19, load_page)) {
		pr_info("invalidated type still alive. test fail\n");
		return -1;
	}

	free_page((unsigned long)store_page_va);
	free_page((unsigned long)load_page_va);
	return 0;
}

static int test_alloc_release_contig(void)
{
	struct page *cma1, *cma2, *cma3;

	cma1 = gcma_alloc_contig(0, 5);
	if (!cma1) {
		pr_err("failed to alloc 5 contig pages\n");
		return -1;
	}
	cma2 = gcma_alloc_contig(0, 10);
	if (!cma2) {
		pr_err("failed to alloc 10 contig pages\n");
		return -1;
	}
	cma3 = gcma_alloc_contig(0, 16);
	if (!cma3) {
		pr_err("failed to alloc 16 contig pages\n");
		return -1;
	}

	gcma_release_contig(0, cma2, 10);
	gcma_release_contig(0, cma1, 5);
	gcma_release_contig(0, cma3, 16);

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

	do_test(test_alloc_release_contig);
	do_test(test_frontswap);
	do_test(test_cleancache);
	do_test(measure_time_alloc);

	return 0;
}

module_init(init_gcma);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Minchan Kim <minchan@kernel.org>");
MODULE_AUTHOR("SeongJae Park <sj38.park@gmail.com>");
MODULE_DESCRIPTION("Test for Guaranteed contiguous memory allocator");
