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
#include <linux/debugfs.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/list.h>

/*********************************
* tunables
**********************************/
/* Enable/disable gcma (disabled by default, fixed at boot for now) */
static bool test_enabled __read_mostly;
module_param_named(enabled, test_enabled, bool, 0);

static bool eval_enabled __read_mostly;
module_param_named(eval_enabled, eval_enabled, bool, 0);

extern void gcma_frontswap_init(unsigned type);
extern int gcma_frontswap_store(unsigned type, pgoff_t offset,
		struct page *page);
extern int gcma_frontswap_load(unsigned type, pgoff_t offset,
		struct page *page);
extern void gcma_frontswap_invalidate_page(unsigned type, pgoff_t offset);
extern void gcma_frontswap_invalidate_area(unsigned type);

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


/**************
 * evaluation
 **************/

struct eval_option {
	int nr_pages;
	int nr_trials;
	unsigned long warmup_ms;
};

struct eval_result {
	unsigned long min;
	unsigned long max;
	unsigned long avg;
};

struct eval_history {
	struct eval_option option;
	unsigned long cma_succ;
	unsigned long cma_fail;
	unsigned long gcma_succ;
	unsigned long gcma_fail;
	struct eval_result cma_alloc_result;
	struct eval_result cma_fail_result;
	struct eval_result cma_release_result;
	struct eval_result gcma_alloc_result;
	struct eval_result gcma_fail_result;
	struct eval_result gcma_release_result;
	struct list_head node;
};

static struct kmem_cache *eval_history_cache;
static LIST_HEAD(eval_history_list);

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

static unsigned long time_diff(struct timespec *start, struct timespec *end)
{
	return end->tv_sec * 1000000000 + end->tv_nsec
		- (start->tv_sec * 1000000000 + start->tv_nsec);
}

/**
 * returns 0 if success, non-zero if failed
 */
static int measure_gcma(int nr_pages,
			unsigned long *alloc_time, unsigned long *release_time)
{
	struct page *page;
	struct timespec start, end;
	getnstimeofday(&start);
	page = gcma_alloc_contig(0, nr_pages);
	getnstimeofday(&end);
	*alloc_time = time_diff(&start, &end);
	if (!page) {
		*release_time = 0;
		return -ENOMEM;
	}

	getnstimeofday(&start);
	gcma_release_contig(0, page, nr_pages);
	getnstimeofday(&end);
	*release_time = time_diff(&start, &end);

	return 0;
}

/**
 * returns 0 if success, non-zero if failed
 */
static int measure_cma(int nr_pages,
			unsigned long *alloc_time, unsigned long *release_time)
{
	struct page *page;
	struct timespec start, end;
	getnstimeofday(&start);
	page = dma_alloc_from_contiguous(NULL, nr_pages, 1);
	getnstimeofday(&end);
	*alloc_time = time_diff(&start, &end);
	if (!page) {
		*release_time = 0;
		return -ENOMEM;
	}

	getnstimeofday(&start);
	dma_release_from_contiguous(NULL, page, nr_pages);
	getnstimeofday(&end);
	*release_time = time_diff(&start, &end);

	return 0;
}

static void set_middle_result(struct eval_result *res, unsigned long time)
{
	res->min = min_of(res->min, time);
	res->max = max_of(res->max, time);
	res->avg += time;
}

static void init_eval_result(struct eval_result *res)
{
	res->min = res->max = res->avg = 0;
}

static void notice_result(struct eval_history *history)
{
	struct eval_result *gcma_alloc, *gcma_fail, *gcma_release;
	struct eval_result *cma_alloc, *cma_fail, *cma_release;

	gcma_alloc = &history->gcma_alloc_result;
	gcma_fail = &history->gcma_fail_result;
	gcma_release = &history->gcma_release_result;

	cma_alloc = &history->cma_alloc_result;
	cma_fail = &history->cma_fail_result;
	cma_release = &history->cma_release_result;

	pr_info("%d page request evaluated.\n"
		"[gcma] succ/fail:%ld/%ld\n"
		"  succ alloc min:%ld\tmax:%ld\tavg:%ld\n"
		"  fail alloc min:%ld\tmax:%ld\tavg:%ld\n"
		"  release min:%ld\tmax:%ld\tavg:%ld\n"
		"[cma] succ/fail:%ld/%ld\n"
		"  succ alloc min:%ld\tmax:%ld\tavg:%ld\n"
		"  fail alloc min:%ld\tmax:%ld\tavg:%ld\n"
		"  release min:%ld\tmax:%ld\tavg:%ld\n",
			history->option.nr_pages,
			history->gcma_succ, history->gcma_fail,
			gcma_alloc->min, gcma_alloc->max, gcma_alloc->avg,
			gcma_fail->min, gcma_fail->max, gcma_fail->avg,
			gcma_release->min, gcma_release->max, gcma_release->avg,

			history->cma_succ, history->cma_fail,
			cma_alloc->min, cma_alloc->max, cma_alloc->avg,
			cma_fail->min, cma_fail->max, cma_fail->avg,
			cma_release->min, cma_release->max, cma_release->avg);
}

static void eval_gcma(struct eval_history *history)
{
	struct eval_option *opt;
	struct eval_result *gcma_alloc_res, *gcma_fail_res;
	struct eval_result *gcma_release_res;
	struct eval_result *cma_alloc_res, *cma_fail_res;
	struct eval_result *cma_release_res;

	unsigned long gcma_alloc_time, gcma_release_time;
	unsigned long cma_alloc_time, cma_release_time;

	int i;

	opt = &history->option;

	history->cma_succ = history->cma_fail = 0;
	history->gcma_succ = history->gcma_fail = 0;

	cma_alloc_res = &history->cma_alloc_result;
	cma_fail_res = &history->cma_fail_result;
	cma_release_res = &history->cma_release_result;

	gcma_alloc_res = &history->gcma_alloc_result;
	gcma_fail_res = &history->gcma_fail_result;
	gcma_release_res = &history->gcma_release_result;

	init_eval_result(cma_alloc_res);
	init_eval_result(cma_fail_res);
	init_eval_result(cma_release_res);

	init_eval_result(gcma_alloc_res);
	init_eval_result(gcma_fail_res);
	init_eval_result(gcma_release_res);

	for (i = 0; i < opt->nr_trials; i++) {
		if (measure_cma(opt->nr_pages,
				&cma_alloc_time, &cma_release_time)) {
			history->cma_fail++;
			set_middle_result(cma_fail_res, cma_alloc_time);
		} else {
			history->cma_succ++;
			set_middle_result(cma_alloc_res, cma_alloc_time);
			set_middle_result(cma_release_res, cma_release_time);
		}
		msleep(opt->warmup_ms);

		if (measure_gcma(opt->nr_pages,
				&gcma_alloc_time, &gcma_release_time)) {
			history->gcma_fail++;
			set_middle_result(gcma_fail_res, gcma_alloc_time);
		} else {
			history->gcma_succ++;
			set_middle_result(gcma_alloc_res, gcma_alloc_time);
			set_middle_result(gcma_release_res, gcma_release_time);
		}
		msleep(opt->warmup_ms);
	}
	if (history->cma_succ > 0) {
		cma_alloc_res->avg = cma_alloc_res->avg / history->cma_succ;
		cma_release_res->avg = cma_release_res->avg / history->cma_succ;
	}
	if (history->cma_fail > 0)
		cma_fail_res->avg = cma_fail_res->avg / history->cma_fail;

	if (history->gcma_succ > 0) {
		gcma_alloc_res->avg = gcma_alloc_res->avg / history->gcma_succ;
		gcma_release_res->avg = gcma_release_res->avg /
						history->gcma_succ;
	}
	if (history->gcma_fail > 0)
		gcma_fail_res->avg = gcma_fail_res->avg / history->gcma_fail;

	notice_result(history);

	list_add_tail(&history->node, &eval_history_list);
}

/**
 * howto_read - just show how to use evaluation feature
 */
static ssize_t howto_read(struct file *filp, char __user *buf,
				size_t length, loff_t *offset)
{
	static char *howto_msg = "howto:\n"
		"\t<# of pages> <# of trials> <warmup_ms> > eval\n";
	char *cursor = howto_msg + *offset;
	int bytes_read = 0;

	while (length && *cursor) {
		put_user(*(cursor++), buf++);

		length--;
		bytes_read++;
	}

	*offset += bytes_read;
	return bytes_read;
}

static const struct file_operations howto_fops = {
	.owner = THIS_MODULE,
	.open = nonseekable_open,
	.read = howto_read,
	.llseek = no_llseek,
};

static ssize_t eval_write(struct file *filp, const char __user *buf,
				size_t length, loff_t *offset)
{
	struct eval_history *history;
	struct eval_option *opt;

	history = kmem_cache_alloc(eval_history_cache, GFP_KERNEL);
	if (history == NULL) {
		pr_warn("failed to alloc eval history\n");
		return length;
	}

	opt = &history->option;

	sscanf(buf, "%d %d %ld", &opt->nr_pages, &opt->nr_trials,
			&opt->warmup_ms);
	pr_info("%s called with %d %d %ld\n", __func__, opt->nr_pages,
			opt->nr_trials, opt->warmup_ms);
	eval_gcma(history);

	return length;
}

static const struct file_operations eval_fops = {
	.owner = THIS_MODULE,
	.open = nonseekable_open,
	.write = eval_write,
	.llseek = no_llseek,
};

static ssize_t eval_res_read(struct file *filp, char __user *buf,
				size_t length, loff_t *offset)
{
	struct eval_history *history;
	struct eval_option *opt;
	struct eval_result *gcma_alloc, *gcma_fail, *gcma_release;
	struct eval_result *cma_alloc, *cma_fail, *cma_release;
	char kbuf[256];
	char *cursor = kbuf;
	int bytes_read = 0;

	pr_info("%s called. length: %d, offset: %d\n",
			__func__, (int)length, (int)*offset);
	if (*offset > 0)
		return 0;

	list_for_each_entry(history, &eval_history_list, node) {
		opt = &history->option;

		gcma_alloc = &history->gcma_alloc_result;
		gcma_fail = &history->gcma_fail_result;
		gcma_release = &history->gcma_release_result;

		cma_alloc = &history->cma_alloc_result;
		cma_fail = &history->cma_fail_result;
		cma_release = &history->cma_release_result;

		notice_result(history);

		sprintf(kbuf, "%d,%ld,%ld,"
				"%ld,%ld,%ld,"
				"%ld,%ld,%ld,"
				"%ld,%ld,%ld,"
				"%ld,%ld,"
				"%ld,%ld,%ld,"
				"%ld,%ld,%ld,"
				"%ld,%ld,%ld\n",
				opt->nr_pages,
				history->gcma_succ, history->gcma_fail,
				gcma_alloc->min, gcma_alloc->max,
					gcma_alloc->avg,
				gcma_fail->min, gcma_fail->max, gcma_fail->avg,
				gcma_release->min, gcma_release->max,
					gcma_release->avg,

				history->cma_succ, history->cma_fail,
				cma_alloc->min, cma_alloc->max, cma_alloc->avg,
				cma_fail->min, cma_fail->max, cma_fail->avg,
				cma_release->min, cma_release->max,
					cma_release->avg);

		cursor = kbuf;
		while (length && *cursor) {
			put_user(*(cursor++), buf++);

			length--;
			bytes_read++;
		}

		*offset += bytes_read;

	}


	return bytes_read;
}

static const struct file_operations result_fops = {
	.owner = THIS_MODULE,
	.open = nonseekable_open,
	.read = eval_res_read,
	.llseek = no_llseek,
};

static struct dentry *debugfs_root;

static int __init debugfs_init(void)
{
	debugfs_root = debugfs_create_dir("gcma_eval", NULL);
	if (!debugfs_root) {
		pr_err("failed to create debugfs\n");
		return -ENOMEM;
	}

	debugfs_create_file("howto", S_IRUSR, debugfs_root, NULL, &howto_fops);
	debugfs_create_file("eval", S_IWUSR, debugfs_root, NULL, &eval_fops);
	debugfs_create_file("res", S_IRUSR, debugfs_root, NULL, &result_fops);
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
	if (test_enabled) {
		pr_info("test gcma\n");

		do_test(test_alloc_release_contig);
		do_test(test_frontswap);
	}

	if (eval_enabled) {
		eval_history_cache = KMEM_CACHE(eval_history, 0);
		if (eval_history_cache == NULL) {
			pr_warn("failed to create evaluation history cache\n");
			return -ENOMEM;
		}
		debugfs_init();
	}

	return 0;
}

module_init(init_gcma);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Minchan Kim <minchan@kernel.org>");
MODULE_AUTHOR("SeongJae Park <sj38.park@gmail.com>");
MODULE_DESCRIPTION("Test for Guaranteed contiguous memory allocator");
