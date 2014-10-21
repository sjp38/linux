/*
 * eval_cma.c - evaluate contiguous memory allocator
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
#include <linux/mm.h>
#include <linux/cma.h>
#include <linux/debugfs.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/sizes.h>

static bool eval_enabled __read_mostly;
module_param_named(eval_enabled, eval_enabled, bool, 0);

struct eval_option {
	int nr_pages;
	int nr_trials;
	unsigned long warmup_ms;
};

struct cma_latency {
	unsigned long min;
	unsigned long max;
	unsigned long avg;
};

struct eval_result {
	struct eval_option option;
	unsigned long nr_succ;
	unsigned long nr_fail;
	struct cma_latency alloc_latency;
	struct cma_latency fail_latency;
	struct cma_latency release_latency;
	struct list_head node;
};

/* Should be initialized during early boot */
struct cma *cma;

static struct kmem_cache *eval_result_cache;
static LIST_HEAD(eval_result_list);

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
static int measure_cma(int nr_pages,
			unsigned long *alloc_time, unsigned long *release_time)
{
	struct page *page;
	struct timespec start, end;
	getnstimeofday(&start);
	page = cma_alloc(cma, nr_pages, 1);
	getnstimeofday(&end);
	*alloc_time = time_diff(&start, &end);
	if (!page) {
		*release_time = 0;
		return -ENOMEM;
	}

	getnstimeofday(&start);
	cma_release(cma, page, nr_pages);
	getnstimeofday(&end);
	*release_time = time_diff(&start, &end);

	return 0;
}

static void set_middle_result(struct cma_latency *lat, unsigned long time)
{
	lat->min = min_of(lat->min, time);
	lat->max = max_of(lat->max, time);
	lat->avg += time;
}

static void init_cma_latency(struct cma_latency *lat)
{
	lat->min = lat->max = lat->avg = 0;
}

static void notice_result(struct eval_result *res)
{
	struct cma_latency *alloc, *fail, *release;

	alloc = &res->alloc_latency;
	fail = &res->fail_latency;
	release = &res->release_latency;

	pr_info("%d page request evaluated.\n"
		"  succ/fail:%ld/%ld\n"
		"  succ alloc min:%ld\tmax:%ld\tavg:%ld\n"
		"  fail alloc min:%ld\tmax:%ld\tavg:%ld\n"
		"  release min:%ld\tmax:%ld\tavg:%ld\n",
			res->option.nr_pages,
			res->nr_succ, res->nr_fail,
			alloc->min, alloc->max, alloc->avg,
			fail->min, fail->max, fail->avg,
			release->min, release->max, release->avg);
}

static void eval_cma(struct eval_result *res)
{
	struct eval_option *opt;
	struct cma_latency *alloc_lat, *fail_lat;
	struct cma_latency *release_lat;

	unsigned long alloc_time, release_time;

	int i;

	opt = &res->option;
	res->nr_succ = res->nr_fail = 0;

	alloc_lat = &res->alloc_latency;
	fail_lat = &res->fail_latency;
	release_lat = &res->release_latency;

	init_cma_latency(alloc_lat);
	init_cma_latency(fail_lat);
	init_cma_latency(release_lat);

	for (i = 0; i < opt->nr_trials; i++) {
		if (measure_cma(opt->nr_pages,
				&alloc_time, &release_time)) {
			res->nr_fail++;
			set_middle_result(fail_lat, alloc_time);
		} else {
			res->nr_succ++;
			set_middle_result(alloc_lat, alloc_time);
			set_middle_result(release_lat, release_time);
		}
		msleep(opt->warmup_ms);
	}
	if (res->nr_succ > 0) {
		alloc_lat->avg = alloc_lat->avg / res->nr_succ;
		release_lat->avg = release_lat->avg / res->nr_succ;
	}
	if (res->nr_fail > 0)
		fail_lat->avg = fail_lat->avg / res->nr_fail;

	notice_result(res);

	list_add_tail(&res->node, &eval_result_list);
}

/**
 * howto_read - just show how to use evaluation feature
 */
static ssize_t howto_read(struct file *filp, char __user *buf,
				size_t length, loff_t *offset)
{
	static char *howto_msg = "howto:\n"
		"\techo '<# of pages> <# of trials> <warmup_ms>' > eval\n";
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
	struct eval_result *result;
	struct eval_option *opt;

	result = kmem_cache_alloc(eval_result_cache, GFP_KERNEL);
	if (result == NULL) {
		pr_warn("failed to alloc eval result\n");
		return length;
	}

	opt = &result->option;

	sscanf(buf, "%d %d %ld", &opt->nr_pages, &opt->nr_trials,
			&opt->warmup_ms);
	pr_info("%s called with %d %d %ld\n", __func__, opt->nr_pages,
			opt->nr_trials, opt->warmup_ms);
	eval_cma(result);

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
	struct eval_result *result;
	struct eval_option *opt;
	struct cma_latency *alloc_lat, *fail_lat, *release_lat;
	char kbuf[256];
	char *cursor = kbuf;
	int bytes_read = 0;

	pr_info("%s called. length: %d, offset: %d\n",
			__func__, (int)length, (int)*offset);
	if (*offset > 0)
		return 0;

	list_for_each_entry(result, &eval_result_list, node) {
		opt = &result->option;

		alloc_lat = &result->alloc_latency;
		fail_lat = &result->fail_latency;
		release_lat = &result->release_latency;

		notice_result(result);

		sprintf(kbuf, "%d,%ld,%ld,"
				"%ld,%ld,%ld,"
				"%ld,%ld,%ld,"
				"%ld,%ld,%ld\n",
				opt->nr_pages,
				result->nr_succ, result->nr_fail,
				alloc_lat->min, alloc_lat->max, alloc_lat->avg,
				fail_lat->min, fail_lat->max, fail_lat->avg,
				release_lat->min, release_lat->max,
					release_lat->avg);

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

void __init eval_cma_contiguous_reserve(phys_addr_t limit)
{
	cma_declare_contiguous(0, CONFIG_EVAL_CMA_AREA_SIZE_MBYTES * SZ_1M, 0,
			0, 0, false, &cma);
}

static int __init init_eval_gcma(void)
{
	if (eval_enabled) {
		eval_result_cache = KMEM_CACHE(eval_result, 0);
		if (eval_result_cache == NULL) {
			pr_warn("failed to create evaluation history cache\n");
			return -ENOMEM;
		}
		debugfs_init();
	}

	return 0;
}

module_init(init_eval_gcma);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Minchan Kim <minchan@kernel.org>");
MODULE_AUTHOR("SeongJae Park <sj38.park@gmail.com>");
MODULE_DESCRIPTION("Evaluation for Contiguous memory allocator");
