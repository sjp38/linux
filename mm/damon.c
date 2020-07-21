// SPDX-License-Identifier: GPL-2.0
/*
 * Data Access Monitor
 *
 * Copyright 2019-2020 Amazon.com, Inc. or its affiliates.
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 *
 * This file is constructed in below parts.
 *
 * - Functions and macros for DAMON data structures
 * - Functions for DAMON core logics and features
 * - Functions for the DAMON programming interface
 * - Functions for the initialization
 */

#define pr_fmt(fmt) "damon: " fmt

#include <linux/damon.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/page_idle.h>
#include <linux/random.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/slab.h>

/* Minimal region size.  Every damon_region is aligned by this. */
#define MIN_REGION PAGE_SIZE

/*
 * Functions and macros for DAMON data structures
 */

#define damon_next_region(r) \
	(container_of(r->list.next, struct damon_region, list))

#define damon_prev_region(r) \
	(container_of(r->list.prev, struct damon_region, list))

#define damon_for_each_region(r, t) \
	list_for_each_entry(r, &t->regions_list, list)

#define damon_for_each_region_safe(r, next, t) \
	list_for_each_entry_safe(r, next, &t->regions_list, list)

#define damon_for_each_target(t, ctx) \
	list_for_each_entry(t, &(ctx)->targets_list, list)

#define damon_for_each_target_safe(t, next, ctx) \
	list_for_each_entry_safe(t, next, &(ctx)->targets_list, list)

/* Get a random number in [l, r) */
#define damon_rand(l, r) (l + prandom_u32() % (r - l))

/*
 * Construct a damon_region struct
 *
 * Returns the pointer to the new struct if success, or NULL otherwise
 */
static struct damon_region *damon_new_region(unsigned long start,
					     unsigned long end)
{
	struct damon_region *region;

	region = kmalloc(sizeof(*region), GFP_KERNEL);
	if (!region)
		return NULL;

	region->ar.start = start;
	region->ar.end = end;
	region->nr_accesses = 0;
	INIT_LIST_HEAD(&region->list);

	return region;
}

/*
 * Add a region between two other regions
 */
static inline void damon_insert_region(struct damon_region *r,
		struct damon_region *prev, struct damon_region *next)
{
	__list_add(&r->list, &prev->list, &next->list);
}

static void damon_add_region(struct damon_region *r, struct damon_target *t)
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

static void damon_destroy_region(struct damon_region *r)
{
	damon_del_region(r);
	damon_free_region(r);
}

/*
 * Construct a damon_target struct
 *
 * Returns the pointer to the new struct if success, or NULL otherwise
 */
static struct damon_target *damon_new_target(unsigned long id)
{
	struct damon_target *t;

	t = kmalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		return NULL;

	t->id = id;
	INIT_LIST_HEAD(&t->regions_list);

	return t;
}

static void damon_add_target(struct damon_ctx *ctx, struct damon_target *t)
{
	list_add_tail(&t->list, &ctx->targets_list);
}

static void damon_del_target(struct damon_target *t)
{
	list_del(&t->list);
}

static void damon_free_target(struct damon_target *t)
{
	struct damon_region *r, *next;

	damon_for_each_region_safe(r, next, t)
		damon_free_region(r);
	kfree(t);
}

static void damon_destroy_target(struct damon_target *t)
{
	damon_del_target(t);
	damon_free_target(t);
}

static unsigned int nr_damon_targets(struct damon_ctx *ctx)
{
	struct damon_target *t;
	unsigned int nr_targets = 0;

	damon_for_each_target(t, ctx)
		nr_targets++;

	return nr_targets;
}

static unsigned int nr_damon_regions(struct damon_target *t)
{
	struct damon_region *r;
	unsigned int nr_regions = 0;

	damon_for_each_region(r, t)
		nr_regions++;

	return nr_regions;
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
 * Reset the aggregated monitoring results
 */
static void kdamond_reset_aggregated(struct damon_ctx *c)
{
	struct damon_task *t;
	struct damon_region *r;

	damon_for_each_task(t, c) {
		damon_for_each_region(r, t)
			r->nr_accesses = 0;
	}
}

/*
 * Check whether current monitoring should be stopped
 *
 * The monitoring is stopped when either the user requested to stop, or all
 * monitoring target tasks are dead.
 *
 * Returns true if need to stop current monitoring.
 */
static bool kdamond_need_stop(struct damon_ctx *ctx)
{
	struct damon_task *t;
	struct task_struct *task;
	bool stop;

	mutex_lock(&ctx->kdamond_lock);
	stop = ctx->kdamond_stop;
	mutex_unlock(&ctx->kdamond_lock);
	if (stop)
		return true;

	damon_for_each_task(t, ctx) {
		/* -1 is reserved for non-process bounded monitoring */
		if (t->pid == -1)
			return false;

		task = damon_get_task_struct(t);
		if (task) {
			put_task_struct(task);
			return false;
		}
	}

	return true;
}

/*
 * The monitoring daemon that runs as a kernel thread
 */
static int kdamond_fn(void *data)
{
	struct damon_ctx *ctx = (struct damon_ctx *)data;
	struct damon_task *t;
	struct damon_region *r, *next;

	pr_info("kdamond (%d) starts\n", ctx->kdamond->pid);
	if (ctx->init_target_regions)
		ctx->init_target_regions(ctx);
	while (!kdamond_need_stop(ctx)) {
		if (ctx->prepare_access_checks)
			ctx->prepare_access_checks(ctx);
		if (ctx->sample_cb)
			ctx->sample_cb(ctx);

		usleep_range(ctx->sample_interval, ctx->sample_interval + 1);

		if (ctx->check_accesses)
			ctx->check_accesses(ctx);

		if (kdamond_aggregate_interval_passed(ctx)) {
			if (ctx->aggregate_cb)
				ctx->aggregate_cb(ctx);
			kdamond_reset_aggregated(ctx);
		}

	}
	damon_for_each_task(t, ctx) {
		damon_for_each_region_safe(r, next, t)
			damon_destroy_region(r);
	}
	pr_debug("kdamond (%d) finishes\n", ctx->kdamond->pid);
	mutex_lock(&ctx->kdamond_lock);
	ctx->kdamond = NULL;
	mutex_unlock(&ctx->kdamond_lock);

	do_exit(0);
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

/**
 * damon_start() - Starts monitoring with given context.
 * @ctx:	monitoring context
 *
 * Return: 0 on success, negative error code otherwise.
 */
int damon_start(struct damon_ctx *ctx)
{
	int err = -EBUSY;

	mutex_lock(&ctx->kdamond_lock);
	if (!ctx->kdamond) {
		err = 0;
		ctx->kdamond_stop = false;
		ctx->kdamond = kthread_create(kdamond_fn, ctx, "kdamond");
		if (IS_ERR(ctx->kdamond))
			err = PTR_ERR(ctx->kdamond);
		else
			wake_up_process(ctx->kdamond);
	}
	mutex_unlock(&ctx->kdamond_lock);

	return err;
}

/**
 * damon_stop() - Stops monitoring of given context.
 * @ctx:	monitoring context
 *
 * Return: 0 on success, negative error code otherwise.
 */
int damon_stop(struct damon_ctx *ctx)
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
 * damon_set_pids() - Set monitoring target processes.
 * @ctx:	monitoring context
 * @pids:	array of target processes pids
 * @nr_pids:	number of entries in @pids
 *
 * This function should not be called while the kdamond is running.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int damon_set_pids(struct damon_ctx *ctx, int *pids, ssize_t nr_pids)
{
	ssize_t i;
	struct damon_task *t, *next;

	damon_for_each_task_safe(t, next, ctx)
		damon_destroy_task(t);

	for (i = 0; i < nr_pids; i++) {
		t = damon_new_task(pids[i]);
		if (!t) {
			pr_err("Failed to alloc damon_task\n");
			return -ENOMEM;
		}
		damon_add_task(ctx, t);
	}

	return 0;
}

/**
 * damon_set_attrs() - Set attributes for the monitoring.
 * @ctx:		monitoring context
 * @sample_int:		time interval between samplings
 * @aggr_int:		time interval between aggregations
 * @nr_reg:		number of regions
 *
 * This function should not be called while the kdamond is running.
 * Every time interval is in micro-seconds.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int damon_set_attrs(struct damon_ctx *ctx, unsigned long sample_int,
		unsigned long aggr_int, unsigned long nr_reg)
{
	if (nr_reg < 3) {
		pr_err("nr_regions (%lu) must be at least 3\n",
				nr_reg);
		return -EINVAL;
	}

	ctx->sample_interval = sample_int;
	ctx->aggr_interval = aggr_int;
	ctx->nr_regions = nr_reg;

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
