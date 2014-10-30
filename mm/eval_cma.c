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
#include <linux/dma-contiguous.h>

#include <linux/eval_cma.h>

static bool eval_disabled __read_mostly;
module_param_named(eval_disabled, eval_disabled, bool, 0);

struct cma_latency {
	unsigned long min;
	unsigned long max;
	unsigned long avg;
};

/*
 * @nr_succ, @nr_fail and @nr_release allocation success / allocation failure /
 * release terminated in larger than (@usecs / 2), equal or smaller than @usecs
 * micro-seconds
 */
struct eval_stat {
	unsigned long usecs;
	unsigned long nr_succ;
	unsigned long nr_fail;
	unsigned long nr_release;

	unsigned long nr_reclaim;
	unsigned long nr_migrate;

	struct list_head node;
};

/*
 * during @nr_eval time @nr_pages cma allocation,
 * reclaim tried @nr_reclaim times and it successfully reclaimed
 * @nr_reclaimed_pages pages.
 * each reclaim call required @reclaim_latency->{min,max,avg} micro seconds
 *
 * loughly, we can say one clean page reclamation requires
 * @reclaim_latency->avg * @nr_reclaim / @nr_reclaimed_pages micro-seconds.
 */
struct eval_result {
	unsigned long nr_pages;
	unsigned long nr_eval;
	unsigned long nr_fail;

	unsigned long nr_reclaim; /* reclaim tries */
	unsigned long nr_reclaimed_pages; /* reclaim success pages */
	unsigned long nr_migrate;
	unsigned long nr_migrated_pages;

	struct cma_latency alloc_latency;
	struct cma_latency fail_latency;
	struct cma_latency release_latency;

	struct cma_latency reclaim_latency;
	struct cma_latency migrate_latency;

	struct list_head node;

	struct list_head stats;
};

/* Should be initialized during early boot */
struct cma *cma;

struct eval_result *current_result;
struct timespec start_time;

static struct kmem_cache *eval_stat_cache;

static struct kmem_cache *eval_result_cache;
static LIST_HEAD(eval_result_list);

static u32 eval_cma_working;

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

static unsigned long ns_to_us(unsigned long nsecs)
{
	return nsecs / 1000;
}

static unsigned long time_diff(struct timespec *start, struct timespec *end)
{
	return (end->tv_sec >= start->tv_sec) ?
		end->tv_nsec - start->tv_nsec :
		1000000000 - (start->tv_nsec - end->tv_nsec);
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
	*alloc_time = ns_to_us(time_diff(&start, &end));
	if (!page) {
		*release_time = 0;
		return -ENOMEM;
	}

	getnstimeofday(&start);
	cma_release(cma, page, nr_pages);
	getnstimeofday(&end);
	*release_time = ns_to_us(time_diff(&start, &end));

	return 0;
}

static void apply_nth_result(unsigned long nth, unsigned long time,
				struct cma_latency *lat)
{
	lat->min = min_of(lat->min, time);
	lat->max = max_of(lat->max, time);
	lat->avg = (lat->avg * (nth - 1) + time) / nth;
}

static void init_cma_latency(struct cma_latency *lat)
{
	lat->min = lat->max = lat->avg = 0;
}

static unsigned long get_expon_larger(unsigned long value)
{
	unsigned long ret = 1;
	while (ret < value)
		ret *= 2;

	return ret;
}

static struct eval_stat *get_stat(unsigned long usecs, struct list_head *list)
{
	struct eval_stat *ret, *iter;
	list_for_each_entry(iter, list, node) {
		if (usecs > iter->usecs / 2 && usecs <= iter->usecs)
			return iter;
	}

	ret = kmem_cache_alloc(eval_stat_cache, GFP_KERNEL);
	if (ret == NULL) {
		pr_warn("failed to alloc eval stat\n");
		goto out;
	}

	ret->usecs = get_expon_larger(usecs);
	ret->nr_succ = ret->nr_fail = ret->nr_release = 0;
	ret->nr_reclaim = ret->nr_migrate = 0;

	list_for_each_entry(iter, list, node) {
		if (iter->usecs > ret->usecs)
			goto hang;
	}

hang:
	list_add(&ret->node, iter->node.prev);

out:
	return ret;
}

static struct eval_result *get_result(unsigned long nr_pages)
{
	struct eval_result *result;
	list_for_each_entry(result, &eval_result_list, node) {
		if (result->nr_pages == nr_pages)
			return result;
	}

	result = kmem_cache_alloc(eval_result_cache, GFP_KERNEL);
	if (result == NULL) {
		pr_warn("failed to alloc eval result\n");
		goto out;
	}

	result->nr_eval = 0;
	result->nr_fail = 0;
	result->nr_reclaim = result->nr_reclaimed_pages = 0;
	result->nr_migrate = result->nr_migrated_pages = 0;

	init_cma_latency(&result->alloc_latency);
	init_cma_latency(&result->fail_latency);
	init_cma_latency(&result->release_latency);
	init_cma_latency(&result->reclaim_latency);
	init_cma_latency(&result->migrate_latency);

	INIT_LIST_HEAD(&result->stats);

	list_add_tail(&result->node, &eval_result_list);
out:
	return result;
}

void eval_cma_reclaim_start(void)
{
	if (current_result == NULL)
		return;

	getnstimeofday(&start_time);
}

void eval_cma_reclaim_end(unsigned long nr_reclaimed)
{
	struct timespec end_time;
	unsigned long time;
	struct eval_stat *stat;

	if (current_result == NULL)
		return;

	getnstimeofday(&end_time);

	time = ns_to_us(time_diff(&start_time, &end_time));

	current_result->nr_reclaim++;
	current_result->nr_reclaimed_pages += nr_reclaimed;

	apply_nth_result(current_result->nr_reclaim, time,
			&current_result->reclaim_latency);

	stat = get_stat(time, &current_result->stats);
	stat->nr_reclaim++;
}

void eval_cma_migrate_start(void)
{
	if (current_result == NULL)
		return;

	getnstimeofday(&start_time);
}

void eval_cma_migrate_end(unsigned long nr_migrated)
{
	struct timespec end_time;
	unsigned long time;
	struct eval_stat *stat;

	if (current_result == NULL || nr_migrated == 0)
		return;

	getnstimeofday(&end_time);

	time = ns_to_us(time_diff(&start_time, &end_time));

	current_result->nr_migrate++;
	current_result->nr_migrated_pages += nr_migrated;

	apply_nth_result(current_result->nr_migrate, time,
			&current_result->migrate_latency);

	stat = get_stat(time, &current_result->stats);
	stat->nr_migrate++;
}


static void eval_cma(struct eval_result *res)
{
	struct cma_latency *alloc_lat, *fail_lat;
	struct cma_latency *release_lat;
	struct eval_stat *stat;

	unsigned long alloc_time, release_time;
	unsigned long nr_succ = 0;

	alloc_lat = &res->alloc_latency;
	fail_lat = &res->fail_latency;
	release_lat = &res->release_latency;

	res->nr_eval++;
	if (measure_cma(res->nr_pages, &alloc_time, &release_time)) {
		res->nr_fail++;
		apply_nth_result(res->nr_fail, alloc_time, fail_lat);

		stat = get_stat(alloc_time, &res->stats);
		stat->nr_fail++;
	} else {
		nr_succ = res->nr_eval - res->nr_fail;

		apply_nth_result(nr_succ, alloc_time, alloc_lat);
		apply_nth_result(nr_succ, release_time, release_lat);

		stat = get_stat(alloc_time, &res->stats);
		stat->nr_succ++;
		stat = get_stat(release_time, &res->stats);
		stat->nr_release++;
	}
}

/**
 * howto_read - just show how to use evaluation feature
 */
static ssize_t howto_read(struct file *filp, char __user *buf,
				size_t length, loff_t *offset)
{
	static char *howto_msg = "howto:\n"
		"\techo <# of pages to request> > eval\n";
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
	unsigned long nr_pages;

	sscanf(buf, "%lu", &nr_pages);
	pr_info("%s called with %lu\n", __func__, nr_pages);

	result = get_result(nr_pages);
	/* warning have been done from get_result() */
	if (result == NULL)
		goto out;

	result->nr_pages = nr_pages;

	eval_cma_working = 1;
	current_result = result;

	eval_cma(result);

	current_result = NULL;
	eval_cma_working = 0;

out:
	return length;
}

static const struct file_operations eval_fops = {
	.owner = THIS_MODULE,
	.open = nonseekable_open,
	.write = eval_write,
	.llseek = no_llseek,
};

static void sprint_res(struct eval_result *res, char *buffer)
{
	struct cma_latency *alloc_lat, *fail_lat, *release_lat;
	struct cma_latency *reclaim_lat, *migrate_lat;

	alloc_lat = &res->alloc_latency;
	fail_lat = &res->fail_latency;
	release_lat = &res->release_latency;
	reclaim_lat = &res->reclaim_latency;
	migrate_lat = &res->migrate_latency;

	sprintf(buffer, "%lu,,%lu,%lu,,"
			"%lu,%lu,%lu,,"
			"%lu,%lu,%lu,,"
			"%lu,%lu,%lu,,"
			"%lu,%lu,%lu,%lu,,"
			"%lu,%lu,%lu,,"
			"%lu,%lu,%lu\n",
			res->nr_pages,
			res->nr_eval, res->nr_fail,
			alloc_lat->min, alloc_lat->max, alloc_lat->avg,
			fail_lat->min, fail_lat->max, fail_lat->avg,
			release_lat->min, release_lat->max,
			release_lat->avg,
			res->nr_reclaim, res->nr_migrate,
			res->nr_reclaimed_pages, res->nr_migrated_pages,
			reclaim_lat->min, reclaim_lat->max, reclaim_lat->avg,
			migrate_lat->min, migrate_lat->max, migrate_lat->avg
			);
}

static ssize_t eval_res_read(struct file *filp, char __user *buf,
				size_t length, loff_t *offset)
{
	struct eval_result *result;
	struct cma_latency *alloc_lat, *fail_lat, *release_lat;
	char kbuf[256];
	char *cursor = kbuf;
	int bytes_read = 0;

	pr_info("%s called. length: %d, offset: %d\n",
			__func__, (int)length, (int)*offset);
	if (*offset > 0)
		return 0;

	list_for_each_entry(result, &eval_result_list, node) {
		alloc_lat = &result->alloc_latency;
		fail_lat = &result->fail_latency;
		release_lat = &result->release_latency;

		sprint_res(result, kbuf);

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

static ssize_t eval_res_hist_read(struct file *filp, char __user *buf,
				size_t length, loff_t *offset)
{
	struct eval_result *result;
	struct eval_stat *stat;
	char kbuf[256];
	char *cursor = kbuf;
	int bytes_read = 0;

	pr_info("%s called. length: %d, offset: %d\n",
			__func__, (int)length, (int)*offset);
	if (*offset > 0)
		return 0;

	list_for_each_entry(result, &eval_result_list, node) {
		list_for_each_entry(stat, &result->stats, node) {
			sprintf(kbuf, "%lu,%lu,,%lu,%lu,%lu,,%lu,%lu\n",
					result->nr_pages, stat->usecs,
					stat->nr_succ, stat->nr_fail,
					stat->nr_release,
					stat->nr_reclaim, stat->nr_migrate);

			cursor = kbuf;
			while (length && *cursor) {
				put_user(*(cursor++), buf++);

				length--;
				bytes_read++;
			}
			*offset += bytes_read;
		}
	}

	return bytes_read;
}

static const struct file_operations result_hist_fops = {
	.owner = THIS_MODULE,
	.open = nonseekable_open,
	.read = eval_res_hist_read,
	.llseek = no_llseek,
};

static struct dentry *debugfs_root;

static int __init debugfs_init(void)
{
	debugfs_root = debugfs_create_dir("cma_eval", NULL);
	if (!debugfs_root) {
		pr_err("failed to create debugfs\n");
		return -ENOMEM;
	}

	debugfs_create_file("howto", S_IRUSR, debugfs_root, NULL, &howto_fops);
	debugfs_create_file("eval", S_IWUSR, debugfs_root, NULL, &eval_fops);
	debugfs_create_file("res", S_IRUSR, debugfs_root, NULL, &result_fops);
	debugfs_create_file("res.hist", S_IRUSR, debugfs_root, NULL,
			&result_hist_fops);
	debugfs_create_bool("working", S_IRUGO, debugfs_root,
			&eval_cma_working);
	return 0;
}

static int __init init_eval_cma(void)
{
	if (eval_disabled) {
		pr_info("eval_cma is disabled. do nothing...");
		return 0;
	}

	cma = dma_contiguous_default_area;
	eval_result_cache = KMEM_CACHE(eval_result, 0);
	if (eval_result_cache == NULL) {
		pr_warn("failed to create evaluation history cache\n");
		return -ENOMEM;
	}

	eval_stat_cache = KMEM_CACHE(eval_stat, 0);
	if (eval_stat_cache == NULL) {
		pr_warn("failed to create evaluation history cache\n");
		return -ENOMEM;
	}

	debugfs_init();

	return 0;
}

module_init(init_eval_cma);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Minchan Kim <minchan@kernel.org>");
MODULE_AUTHOR("SeongJae Park <sj38.park@gmail.com>");
MODULE_DESCRIPTION("Evaluation for Contiguous memory allocator");
