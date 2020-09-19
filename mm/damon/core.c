// SPDX-License-Identifier: GPL-2.0
/*
 * Data Access Monitor
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 *
 * This file is constructed in below parts.
 *
 * - Functions and macros for DAMON data structures
 * - Functions for the initial monitoring target regions construction
 * - Functions for the dynamic monitoring target regions update
 * - Functions for the access checking of the regions
 * - Functions for the target validity check and cleanup
 * - Functions for DAMON core logics and features
 * - Functions for the DAMON programming interface
 * - Functions for the initialization
 */

#define pr_fmt(fmt) "damon: " fmt

#include <asm-generic/mman-common.h>
#include <linux/damon.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/memory_hotplug.h>
#include <linux/mm.h>
#include <linux/mmu_notifier.h>
#include <linux/module.h>
#include <linux/page_idle.h>
#include <linux/pagemap.h>
#include <linux/random.h>
#include <linux/rmap.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/slab.h>

#include "damon.h"

#define CREATE_TRACE_POINTS
#include <trace/events/damon.h>

/* Minimal region size.  Every damon_region is aligned by this. */
#ifndef CONFIG_DAMON_KUNIT_TEST
#define MIN_REGION PAGE_SIZE
#else
#define MIN_REGION 1
#endif

/*
 * Functions and macros for DAMON data structures
 */

static DEFINE_MUTEX(damon_lock);
static int nr_running_ctxs;

/*
 * Construct a damon_region struct
 *
 * Returns the pointer to the new struct if success, or NULL otherwise
 */
struct damon_region *damon_new_region(unsigned long start, unsigned long end)
{
	struct damon_region *region;

	region = kmalloc(sizeof(*region), GFP_KERNEL);
	if (!region)
		return NULL;

	region->ar.start = start;
	region->ar.end = end;
	region->nr_accesses = 0;
	INIT_LIST_HEAD(&region->list);

	region->age = 0;
	region->last_nr_accesses = 0;

	return region;
}

/*
 * Add a region between two other regions
 */
inline void damon_insert_region(struct damon_region *r,
		struct damon_region *prev, struct damon_region *next)
{
	__list_add(&r->list, &prev->list, &next->list);
}

void damon_add_region(struct damon_region *r, struct damon_target *t)
{
	list_add_tail(&r->list, &t->regions_list);
}

static void damon_del_region(struct damon_region *r)
{
	list_del(&r->list);
}

static void damon_free_region(struct damon_region *r)
{
	kfree(r);
}

void damon_destroy_region(struct damon_region *r)
{
	damon_del_region(r);
	damon_free_region(r);
}

/*
 * Construct a damon_target struct
 *
 * Returns the pointer to the new struct if success, or NULL otherwise
 */
struct damon_target *damon_new_target(unsigned long id)
{
	struct damon_target *t;

	t = kmalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		return NULL;

	t->id = id;
	INIT_LIST_HEAD(&t->regions_list);

	return t;
}

void damon_add_target(struct damon_ctx *ctx, struct damon_target *t)
{
	list_add_tail(&t->list, &ctx->targets_list);
}

static void damon_del_target(struct damon_target *t)
{
	list_del(&t->list);
}

void damon_free_target(struct damon_target *t)
{
	struct damon_region *r, *next;

	damon_for_each_region_safe(r, next, t)
		damon_free_region(r);
	kfree(t);
}

void damon_destroy_target(struct damon_target *t)
{
	damon_del_target(t);
	damon_free_target(t);
}

struct damos *damon_new_scheme(
		unsigned long min_sz_region, unsigned long max_sz_region,
		unsigned int min_nr_accesses, unsigned int max_nr_accesses,
		unsigned int min_age_region, unsigned int max_age_region,
		enum damos_action action)
{
	struct damos *scheme;

	scheme = kmalloc(sizeof(*scheme), GFP_KERNEL);
	if (!scheme)
		return NULL;
	scheme->min_sz_region = min_sz_region;
	scheme->max_sz_region = max_sz_region;
	scheme->min_nr_accesses = min_nr_accesses;
	scheme->max_nr_accesses = max_nr_accesses;
	scheme->min_age_region = min_age_region;
	scheme->max_age_region = max_age_region;
	scheme->action = action;
	scheme->stat_count = 0;
	scheme->stat_sz = 0;
	INIT_LIST_HEAD(&scheme->list);

	return scheme;
}

void damon_add_scheme(struct damon_ctx *ctx, struct damos *s)
{
	list_add_tail(&s->list, &ctx->schemes_list);
}

static void damon_del_scheme(struct damos *s)
{
	list_del(&s->list);
}

static void damon_free_scheme(struct damos *s)
{
	kfree(s);
}

void damon_destroy_scheme(struct damos *s)
{
	damon_del_scheme(s);
	damon_free_scheme(s);
}

void damon_set_vaddr_primitives(struct damon_ctx *ctx)
{
	ctx->init_target_regions = kdamond_init_vm_regions;
	ctx->update_target_regions = kdamond_update_vm_regions;
	ctx->prepare_access_checks = kdamond_prepare_vm_access_checks;
	ctx->check_accesses = kdamond_check_vm_accesses;
	ctx->target_valid = kdamond_vm_target_valid;
	ctx->cleanup = kdamond_vm_cleanup;
}

void damon_set_paddr_primitives(struct damon_ctx *ctx)
{
	ctx->init_target_regions = kdamond_init_phys_regions;
	ctx->update_target_regions = kdamond_update_phys_regions;
	ctx->prepare_access_checks = kdamond_prepare_phys_access_checks;
	ctx->check_accesses = kdamond_check_phys_accesses;
	ctx->target_valid = NULL;
	ctx->cleanup = NULL;
}

struct damon_ctx *damon_new_ctx(void)
{
	struct damon_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	ctx->sample_interval = 5 * 1000;
	ctx->aggr_interval = 100 * 1000;
	ctx->regions_update_interval = 1000 * 1000;
	ctx->min_nr_regions = 10;
	ctx->max_nr_regions = 1000;

	damon_set_vaddr_primitives(ctx);

	ktime_get_coarse_ts64(&ctx->last_aggregation);
	ctx->last_regions_update = ctx->last_aggregation;

	if (damon_set_recording(ctx, 0, "none")) {
		kfree(ctx);
		return NULL;
	}

	mutex_init(&ctx->kdamond_lock);

	INIT_LIST_HEAD(&ctx->targets_list);
	INIT_LIST_HEAD(&ctx->schemes_list);

	return ctx;
}

void damon_destroy_ctx(struct damon_ctx *ctx)
{
	struct damon_target *t, *next_t;
	struct damos *s, *next_s;

	damon_for_each_target_safe(t, next_t, ctx)
		damon_destroy_target(t);

	damon_for_each_scheme_safe(s, next_s, ctx)
		damon_destroy_scheme(s);

	kfree(ctx);
}

static unsigned int nr_damon_targets(struct damon_ctx *ctx)
{
	struct damon_target *t;
	unsigned int nr_targets = 0;

	damon_for_each_target(t, ctx)
		nr_targets++;

	return nr_targets;
}

unsigned int nr_damon_regions(struct damon_target *t)
{
	struct damon_region *r;
	unsigned int nr_regions = 0;

	damon_for_each_region(r, t)
		nr_regions++;

	return nr_regions;
}

/* Returns the size upper limit for each monitoring region */
static unsigned long damon_region_sz_limit(struct damon_ctx *ctx)
{
	struct damon_target *t;
	struct damon_region *r;
	unsigned long sz = 0;

	damon_for_each_target(t, ctx) {
		damon_for_each_region(r, t)
			sz += r->ar.end - r->ar.start;
	}

	if (ctx->min_nr_regions)
		sz /= ctx->min_nr_regions;
	if (sz < MIN_REGION)
		sz = MIN_REGION;

	return sz;
}

/*
 * Functions for DAMON core logics and features
 */

/*
 * damon_check_reset_time_interval() - Check if a time interval is elapsed.
 * @baseline:	the time to check whether the interval has elapsed since
 * @interval:	the time interval (microseconds)
 *
 * See whether the given time interval has passed since the given baseline
 * time.  If so, it also updates the baseline to current time for next check.
 *
 * Return:	true if the time interval has passed, or false otherwise.
 */
static bool damon_check_reset_time_interval(struct timespec64 *baseline,
		unsigned long interval)
{
	struct timespec64 now;

	ktime_get_coarse_ts64(&now);
	if ((timespec64_to_ns(&now) - timespec64_to_ns(baseline)) <
			interval * 1000)
		return false;
	*baseline = now;
	return true;
}

/*
 * Check whether it is time to flush the aggregated information
 */
static bool kdamond_aggregate_interval_passed(struct damon_ctx *ctx)
{
	return damon_check_reset_time_interval(&ctx->last_aggregation,
			ctx->aggr_interval);
}

/*
 * Flush the content in the result buffer to the result file
 */
static void damon_flush_rbuffer(struct damon_ctx *ctx)
{
	ssize_t sz;
	loff_t pos = 0;
	struct file *rfile;

	if (!ctx->rbuf_offset)
		return;

	rfile = filp_open(ctx->rfile_path,
			O_CREAT | O_RDWR | O_APPEND | O_LARGEFILE, 0644);
	if (IS_ERR(rfile)) {
		pr_err("Cannot open the result file %s\n",
				ctx->rfile_path);
		return;
	}

	while (ctx->rbuf_offset) {
		sz = kernel_write(rfile, ctx->rbuf, ctx->rbuf_offset, &pos);
		if (sz < 0)
			break;
		ctx->rbuf_offset -= sz;
	}
	filp_close(rfile, NULL);
}

/*
 * Write a data into the result buffer
 */
static void damon_write_rbuf(struct damon_ctx *ctx, void *data, ssize_t size)
{
	if (!ctx->rbuf_len || !ctx->rbuf || !ctx->rfile_path)
		return;
	if (ctx->rbuf_offset + size > ctx->rbuf_len)
		damon_flush_rbuffer(ctx);
	if (ctx->rbuf_offset + size > ctx->rbuf_len) {
		pr_warn("%s: flush failed, or wrong size given(%u, %zu)\n",
				__func__, ctx->rbuf_offset, size);
		return;
	}

	memcpy(&ctx->rbuf[ctx->rbuf_offset], data, size);
	ctx->rbuf_offset += size;
}

/*
 * Flush the aggregated monitoring results to the result buffer
 *
 * Stores current tracking results to the result buffer and reset 'nr_accesses'
 * of each region.  The format for the result buffer is as below:
 *
 *   <time> <number of targets> <array of target infos>
 *
 *   target info: <id> <number of regions> <array of region infos>
 *   region info: <start address> <end address> <nr_accesses>
 */
static void kdamond_reset_aggregated(struct damon_ctx *c)
{
	struct damon_target *t;
	struct timespec64 now;
	unsigned int nr;

	ktime_get_coarse_ts64(&now);

	damon_write_rbuf(c, &now, sizeof(now));
	nr = nr_damon_targets(c);
	damon_write_rbuf(c, &nr, sizeof(nr));

	damon_for_each_target(t, c) {
		struct damon_region *r;

		damon_write_rbuf(c, &t->id, sizeof(t->id));
		nr = nr_damon_regions(t);
		damon_write_rbuf(c, &nr, sizeof(nr));
		damon_for_each_region(r, t) {
			damon_write_rbuf(c, &r->ar.start, sizeof(r->ar.start));
			damon_write_rbuf(c, &r->ar.end, sizeof(r->ar.end));
			damon_write_rbuf(c, &r->nr_accesses,
					sizeof(r->nr_accesses));
			trace_damon_aggregated(t, r, nr);
			r->last_nr_accesses = r->nr_accesses;
			r->nr_accesses = 0;
		}
	}
}

#ifndef CONFIG_ADVISE_SYSCALLS
static int damos_madvise(struct damon_target *target, struct damon_region *r,
			int behavior)
{
	return -EINVAL;
}
#else
static int damos_madvise(struct damon_target *target, struct damon_region *r,
			int behavior)
{
	struct task_struct *t;
	struct mm_struct *mm;
	int ret = -ENOMEM;

	t = damon_get_task_struct(target);
	if (!t)
		goto out;
	mm = damon_get_mm(target);
	if (!mm)
		goto put_task_out;

	ret = do_madvise(t, mm, PAGE_ALIGN(r->ar.start),
			PAGE_ALIGN(r->ar.end - r->ar.start), behavior);
	mmput(mm);
put_task_out:
	put_task_struct(t);
out:
	return ret;
}
#endif	/* CONFIG_ADVISE_SYSCALLS */

static int damos_do_action(struct damon_target *target, struct damon_region *r,
			enum damos_action action)
{
	int madv_action;

	switch (action) {
	case DAMOS_WILLNEED:
		madv_action = MADV_WILLNEED;
		break;
	case DAMOS_COLD:
		madv_action = MADV_COLD;
		break;
	case DAMOS_PAGEOUT:
		madv_action = MADV_PAGEOUT;
		break;
	case DAMOS_HUGEPAGE:
		madv_action = MADV_HUGEPAGE;
		break;
	case DAMOS_NOHUGEPAGE:
		madv_action = MADV_NOHUGEPAGE;
		break;
	case DAMOS_STAT:
		return 0;
	default:
		pr_warn("Wrong action %d\n", action);
		return -EINVAL;
	}

	return damos_madvise(target, r, madv_action);
}

static void damon_do_apply_schemes(struct damon_ctx *c,
				   struct damon_target *t,
				   struct damon_region *r)
{
	struct damos *s;
	unsigned long sz;

	damon_for_each_scheme(s, c) {
		sz = r->ar.end - r->ar.start;
		if (sz < s->min_sz_region || s->max_sz_region < sz)
			continue;
		if (r->nr_accesses < s->min_nr_accesses ||
				s->max_nr_accesses < r->nr_accesses)
			continue;
		if (r->age < s->min_age_region || s->max_age_region < r->age)
			continue;
		s->stat_count++;
		s->stat_sz += sz;
		damos_do_action(t, r, s->action);
		if (s->action != DAMOS_STAT)
			r->age = 0;
	}
}

static void kdamond_apply_schemes(struct damon_ctx *c)
{
	struct damon_target *t;
	struct damon_region *r;

	damon_for_each_target(t, c) {
		damon_for_each_region(r, t)
			damon_do_apply_schemes(c, t, r);
	}
}

#define sz_damon_region(r) (r->ar.end - r->ar.start)

/*
 * Merge two adjacent regions into one region
 */
static void damon_merge_two_regions(struct damon_region *l,
				struct damon_region *r)
{
	unsigned long sz_l = sz_damon_region(l), sz_r = sz_damon_region(r);

	l->nr_accesses = (l->nr_accesses * sz_l + r->nr_accesses * sz_r) /
			(sz_l + sz_r);
	l->age = (l->age * sz_l + r->age * sz_r) / (sz_l + sz_r);
	l->ar.end = r->ar.end;
	damon_destroy_region(r);
}

#define diff_of(a, b) (a > b ? a - b : b - a)

/*
 * Merge adjacent regions having similar access frequencies
 *
 * t		target affected by this merge operation
 * thres	'->nr_accesses' diff threshold for the merge
 * sz_limit	size upper limit of each region
 */
static void damon_merge_regions_of(struct damon_target *t, unsigned int thres,
				   unsigned long sz_limit)
{
	struct damon_region *r, *prev = NULL, *next;

	damon_for_each_region_safe(r, next, t) {
		if (diff_of(r->nr_accesses, r->last_nr_accesses) > thres)
			r->age = 0;
		else
			r->age++;

		if (prev && prev->ar.end == r->ar.start &&
		    diff_of(prev->nr_accesses, r->nr_accesses) <= thres &&
		    sz_damon_region(prev) + sz_damon_region(r) <= sz_limit)
			damon_merge_two_regions(prev, r);
		else
			prev = r;
	}
}

/*
 * Merge adjacent regions having similar access frequencies
 *
 * threshold	'->nr_accesses' diff threshold for the merge
 * sz_limit	size upper limit of each region
 *
 * This function merges monitoring target regions which are adjacent and their
 * access frequencies are similar.  This is for minimizing the monitoring
 * overhead under the dynamically changeable access pattern.  If a merge was
 * unnecessarily made, later 'kdamond_split_regions()' will revert it.
 */
static void kdamond_merge_regions(struct damon_ctx *c, unsigned int threshold,
				  unsigned long sz_limit)
{
	struct damon_target *t;

	damon_for_each_target(t, c)
		damon_merge_regions_of(t, threshold, sz_limit);
}

/*
 * Split a region in two
 *
 * r		the region to be split
 * sz_r		size of the first sub-region that will be made
 */
static void damon_split_region_at(struct damon_ctx *ctx,
				  struct damon_region *r, unsigned long sz_r)
{
	struct damon_region *new;

	new = damon_new_region(r->ar.start + sz_r, r->ar.end);
	r->ar.end = new->ar.start;

	new->age = r->age;
	new->last_nr_accesses = r->last_nr_accesses;

	damon_insert_region(new, r, damon_next_region(r));
}

/* Split every region in the given target into 'nr_subs' regions */
static void damon_split_regions_of(struct damon_ctx *ctx,
				     struct damon_target *t, int nr_subs)
{
	struct damon_region *r, *next;
	unsigned long sz_region, sz_sub = 0;
	int i;

	damon_for_each_region_safe(r, next, t) {
		sz_region = r->ar.end - r->ar.start;

		for (i = 0; i < nr_subs - 1 &&
				sz_region > 2 * MIN_REGION; i++) {
			/*
			 * Randomly select size of left sub-region to be at
			 * least 10 percent and at most 90% of original region
			 */
			sz_sub = ALIGN_DOWN(damon_rand(1, 10) *
					sz_region / 10, MIN_REGION);
			/* Do not allow blank region */
			if (sz_sub == 0 || sz_sub >= sz_region)
				continue;

			damon_split_region_at(ctx, r, sz_sub);
			sz_region = sz_sub;
		}
	}
}

/*
 * Split every target region into randomly-sized small regions
 *
 * This function splits every target region into random-sized small regions if
 * current total number of the regions is equal or smaller than half of the
 * user-specified maximum number of regions.  This is for maximizing the
 * monitoring accuracy under the dynamically changeable access patterns.  If a
 * split was unnecessarily made, later 'kdamond_merge_regions()' will revert
 * it.
 */
static void kdamond_split_regions(struct damon_ctx *ctx)
{
	struct damon_target *t;
	unsigned int nr_regions = 0;
	static unsigned int last_nr_regions;
	int nr_subregions = 2;

	damon_for_each_target(t, ctx)
		nr_regions += nr_damon_regions(t);

	if (nr_regions > ctx->max_nr_regions / 2)
		return;

	/* Maybe the middle of the region has different access frequency */
	if (last_nr_regions == nr_regions &&
			nr_regions < ctx->max_nr_regions / 3)
		nr_subregions = 3;

	damon_for_each_target(t, ctx)
		damon_split_regions_of(ctx, t, nr_subregions);

	last_nr_regions = nr_regions;
}

/*
 * Check whether it is time to check and apply the target monitoring regions
 *
 * Returns true if it is.
 */
static bool kdamond_need_update_regions(struct damon_ctx *ctx)
{
	return damon_check_reset_time_interval(&ctx->last_regions_update,
			ctx->regions_update_interval);
}

/*
 * Check whether current monitoring should be stopped
 *
 * The monitoring is stopped when either the user requested to stop, or all
 * monitoring targets are invalid.
 *
 * Returns true if need to stop current monitoring.
 */
static bool kdamond_need_stop(struct damon_ctx *ctx)
{
	struct damon_target *t;
	bool stop;

	mutex_lock(&ctx->kdamond_lock);
	stop = ctx->kdamond_stop;
	mutex_unlock(&ctx->kdamond_lock);
	if (stop)
		return true;

	if (!ctx->target_valid)
		return false;

	damon_for_each_target(t, ctx) {
		if (ctx->target_valid(t))
			return false;
	}

	return true;
}

static void kdamond_write_record_header(struct damon_ctx *ctx)
{
	int recfmt_ver = 2;

	damon_write_rbuf(ctx, "damon_recfmt_ver", 16);
	damon_write_rbuf(ctx, &recfmt_ver, sizeof(recfmt_ver));
}

/*
 * The monitoring daemon that runs as a kernel thread
 */
static int kdamond_fn(void *data)
{
	struct damon_ctx *ctx = (struct damon_ctx *)data;
	struct damon_target *t;
	struct damon_region *r, *next;
	unsigned int max_nr_accesses = 0;
	unsigned long sz_limit = 0;

	pr_info("kdamond (%d) starts\n", ctx->kdamond->pid);
	if (ctx->init_target_regions)
		ctx->init_target_regions(ctx);
	sz_limit = damon_region_sz_limit(ctx);

	kdamond_write_record_header(ctx);

	while (!kdamond_need_stop(ctx)) {
		if (ctx->prepare_access_checks)
			ctx->prepare_access_checks(ctx);
		if (ctx->sample_cb)
			ctx->sample_cb(ctx);

		usleep_range(ctx->sample_interval, ctx->sample_interval + 1);

		if (ctx->check_accesses)
			max_nr_accesses = ctx->check_accesses(ctx);

		if (kdamond_aggregate_interval_passed(ctx)) {
			if (ctx->aggregate_cb)
				ctx->aggregate_cb(ctx);
			kdamond_merge_regions(ctx, max_nr_accesses / 10,
					sz_limit);
			kdamond_apply_schemes(ctx);
			kdamond_reset_aggregated(ctx);
			kdamond_split_regions(ctx);
		}

		if (kdamond_need_update_regions(ctx)) {
			if (ctx->update_target_regions)
				ctx->update_target_regions(ctx);
			sz_limit = damon_region_sz_limit(ctx);
		}
	}
	damon_flush_rbuffer(ctx);
	damon_for_each_target(t, ctx) {
		damon_for_each_region_safe(r, next, t)
			damon_destroy_region(r);
	}

	if (ctx->cleanup)
		ctx->cleanup(ctx);

	pr_debug("kdamond (%d) finishes\n", ctx->kdamond->pid);
	mutex_lock(&ctx->kdamond_lock);
	ctx->kdamond = NULL;
	mutex_unlock(&ctx->kdamond_lock);

	mutex_lock(&damon_lock);
	nr_running_ctxs--;
	mutex_unlock(&damon_lock);

	do_exit(0);
}

/**
 * damon_nr_running_ctxs() - Return number of currently running contexts.
 */
int damon_nr_running_ctxs(void)
{
	int nr_ctxs;

	mutex_lock(&damon_lock);
	nr_ctxs = nr_running_ctxs;
	mutex_unlock(&damon_lock);

	return nr_ctxs;
}

/*
 * Functions for the DAMON programming interface
 */

static bool damon_kdamond_running(struct damon_ctx *ctx)
{
	bool running;

	mutex_lock(&ctx->kdamond_lock);
	running = ctx->kdamond != NULL;
	mutex_unlock(&ctx->kdamond_lock);

	return running;
}

/*
 * __damon_start() - Starts monitoring with given context.
 * @ctx:	monitoring context
 *
 * This function should be called while damon_lock is hold.
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int __damon_start(struct damon_ctx *ctx)
{
	int err = -EBUSY;

	mutex_lock(&ctx->kdamond_lock);
	if (!ctx->kdamond) {
		err = 0;
		ctx->kdamond_stop = false;
		ctx->kdamond = kthread_create(kdamond_fn, ctx, "kdamond.%d.%d",
				current ? current->pid : 0, nr_running_ctxs);
		if (IS_ERR(ctx->kdamond))
			err = PTR_ERR(ctx->kdamond);
		else
			wake_up_process(ctx->kdamond);
	}
	mutex_unlock(&ctx->kdamond_lock);

	return err;
}

/**
 * damon_start() - Starts the monitorings for a given group of contexts.
 * @ctxs:	an array of the contexts to start monitoring
 * @nr_ctxs:	size of @ctxs
 *
 * This function starts a group of monitoring threads for a group of monitoring
 * contexts.  One thread per each context is created and run concurrently.  The
 * caller should handle synchronization between the threads by itself.  If a
 * group of threads that created by other 'damon_start()' call is currently
 * running, this function does nothing but returns -EBUSY.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int damon_start(struct damon_ctx *ctxs, int nr_ctxs)
{
	int i;
	int err = 0;

	mutex_lock(&damon_lock);
	if (nr_running_ctxs) {
		mutex_unlock(&damon_lock);
		return -EBUSY;
	}

	for (i = 0; i < nr_ctxs; i++) {
		err = __damon_start(&ctxs[i]);
		if (err)
			break;
		nr_running_ctxs++;
	}
	mutex_unlock(&damon_lock);

	return err;
}

int damon_start_ctx_ptrs(struct damon_ctx **ctxs, int nr_ctxs)
{
	int i;
	int err = 0;

	mutex_lock(&damon_lock);
	if (nr_running_ctxs) {
		mutex_unlock(&damon_lock);
		return -EBUSY;
	}

	for (i = 0; i < nr_ctxs; i++) {
		err = __damon_start(ctxs[i]);
		if (err)
			break;
		nr_running_ctxs++;
	}
	mutex_unlock(&damon_lock);

	return err;
}

/*
 * __damon_stop() - Stops monitoring of given context.
 * @ctx:	monitoring context
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int __damon_stop(struct damon_ctx *ctx)
{
	mutex_lock(&ctx->kdamond_lock);
	if (ctx->kdamond) {
		ctx->kdamond_stop = true;
		mutex_unlock(&ctx->kdamond_lock);
		while (damon_kdamond_running(ctx))
			usleep_range(ctx->sample_interval,
					ctx->sample_interval * 2);
		return 0;
	}
	mutex_unlock(&ctx->kdamond_lock);

	return -EPERM;
}

/**
 * damon_stop() - Stops the monitorings for a given group of contexts.
 * @ctxs:	an array of the contexts to stop monitoring
 * @nr_ctxs:	size of @ctxs
 *
 * Return: 0 on success, negative error code otherwise.
 */
int damon_stop(struct damon_ctx *ctxs, int nr_ctxs)
{
	int i, err = 0;

	for (i = 0; i < nr_ctxs; i++) {
		/* nr_running_ctxs is decremented in kdamond_fn */
		err = __damon_stop(&ctxs[i]);
		if (err)
			return err;
	}

	return err;
}

int damon_stop_ctx_ptrs(struct damon_ctx **ctxs, int nr_ctxs)
{
	int i, err = 0;

	for (i = 0; i < nr_ctxs; i++) {
		/* nr_running_ctxs is decremented in kdamond_fn */
		err = __damon_stop(ctxs[i]);
		if (err)
			return err;
	}

	return err;
}

/**
 * damon_set_schemes() - Set data access monitoring based operation schemes.
 * @ctx:	monitoring context
 * @schemes:	array of the schemes
 * @nr_schemes:	number of entries in @schemes
 *
 * This function should not be called while the kdamond of the context is
 * running.
 *
 * Return: 0 if success, or negative error code otherwise.
 */
int damon_set_schemes(struct damon_ctx *ctx, struct damos **schemes,
			ssize_t nr_schemes)
{
	struct damos *s, *next;
	ssize_t i;

	damon_for_each_scheme_safe(s, next, ctx)
		damon_destroy_scheme(s);
	for (i = 0; i < nr_schemes; i++)
		damon_add_scheme(ctx, schemes[i]);
	return 0;
}

/**
 * damon_set_targets() - Set monitoring targets.
 * @ctx:	monitoring context
 * @ids:	array of target ids
 * @nr_ids:	number of entries in @ids
 *
 * This function should not be called while the kdamond is running.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int damon_set_targets(struct damon_ctx *ctx,
		      unsigned long *ids, ssize_t nr_ids)
{
	ssize_t i;
	struct damon_target *t, *next;

	damon_for_each_target_safe(t, next, ctx)
		damon_destroy_target(t);

	for (i = 0; i < nr_ids; i++) {
		t = damon_new_target(ids[i]);
		if (!t) {
			pr_err("Failed to alloc damon_target\n");
			return -ENOMEM;
		}
		damon_add_target(ctx, t);
	}

	return 0;
}

/**
 * damon_set_recording() - Set attributes for the recording.
 * @ctx:	target kdamond context
 * @rbuf_len:	length of the result buffer
 * @rfile_path:	path to the monitor result files
 *
 * Setting 'rbuf_len' 0 disables recording.
 *
 * This function should not be called while the kdamond is running.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int damon_set_recording(struct damon_ctx *ctx,
			unsigned int rbuf_len, char *rfile_path)
{
	size_t rfile_path_len;

	if (rbuf_len && (rbuf_len > MAX_RECORD_BUFFER_LEN ||
			rbuf_len < MIN_RECORD_BUFFER_LEN)) {
		pr_err("result buffer size (%u) is out of [%d,%d]\n",
				rbuf_len, MIN_RECORD_BUFFER_LEN,
				MAX_RECORD_BUFFER_LEN);
		return -EINVAL;
	}
	rfile_path_len = strnlen(rfile_path, MAX_RFILE_PATH_LEN);
	if (rfile_path_len >= MAX_RFILE_PATH_LEN) {
		pr_err("too long (>%d) result file path %s\n",
				MAX_RFILE_PATH_LEN, rfile_path);
		return -EINVAL;
	}
	ctx->rbuf_len = rbuf_len;
	kfree(ctx->rbuf);
	ctx->rbuf = NULL;
	kfree(ctx->rfile_path);
	ctx->rfile_path = NULL;

	if (rbuf_len) {
		ctx->rbuf = kvmalloc(rbuf_len, GFP_KERNEL);
		if (!ctx->rbuf)
			return -ENOMEM;
	}
	ctx->rfile_path = kmalloc(rfile_path_len + 1, GFP_KERNEL);
	if (!ctx->rfile_path)
		return -ENOMEM;
	strncpy(ctx->rfile_path, rfile_path, rfile_path_len + 1);
	return 0;
}

/**
 * damon_set_attrs() - Set attributes for the monitoring.
 * @ctx:		monitoring context
 * @sample_int:		time interval between samplings
 * @regions_update_int:	time interval between target regions update
 * @aggr_int:		time interval between aggregations
 * @min_nr_reg:		minimal number of regions
 * @max_nr_reg:		maximum number of regions
 *
 * This function should not be called while the kdamond is running.
 * Every time interval is in micro-seconds.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int damon_set_attrs(struct damon_ctx *ctx, unsigned long sample_int,
		    unsigned long aggr_int, unsigned long regions_update_int,
		    unsigned long min_nr_reg, unsigned long max_nr_reg)
{
	if (min_nr_reg < 3) {
		pr_err("min_nr_regions (%lu) must be at least 3\n",
				min_nr_reg);
		return -EINVAL;
	}
	if (min_nr_reg > max_nr_reg) {
		pr_err("invalid nr_regions.  min (%lu) > max (%lu)\n",
				min_nr_reg, max_nr_reg);
		return -EINVAL;
	}

	ctx->sample_interval = sample_int;
	ctx->aggr_interval = aggr_int;
	ctx->regions_update_interval = regions_update_int;
	ctx->min_nr_regions = min_nr_reg;
	ctx->max_nr_regions = max_nr_reg;

	return 0;
}

/*
 * Functions for the initialization
 */

static int __init damon_init(void)
{
	return 0;
}

module_init(damon_init);

#include "core-test.h"
