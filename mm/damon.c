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
 * - Functions for the initialization
 *
 * The core parts are not implemented yet.
 */

#define pr_fmt(fmt) "damon: " fmt

#include <linux/damon.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>

/*
 * Functions and macros for DAMON data structures
 */

#define damon_get_task_struct(t) \
	(get_pid_task(find_vpid(t->pid), PIDTYPE_PID))

#define damon_next_region(r) \
	(container_of(r->list.next, struct damon_region, list))

#define damon_prev_region(r) \
	(container_of(r->list.prev, struct damon_region, list))

#define damon_for_each_region(r, t) \
	list_for_each_entry(r, &t->regions_list, list)

#define damon_for_each_region_safe(r, next, t) \
	list_for_each_entry_safe(r, next, &t->regions_list, list)

#define damon_for_each_task(t, ctx) \
	list_for_each_entry(t, &(ctx)->tasks_list, list)

#define damon_for_each_task_safe(t, next, ctx) \
	list_for_each_entry_safe(t, next, &(ctx)->tasks_list, list)

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

static void damon_add_region(struct damon_region *r, struct damon_task *t)
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
 * Construct a damon_task struct
 *
 * Returns the pointer to the new struct if success, or NULL otherwise
 */
static struct damon_task *damon_new_task(int pid)
{
	struct damon_task *t;

	t = kmalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		return NULL;

	t->pid = pid;
	INIT_LIST_HEAD(&t->regions_list);

	return t;
}

static void damon_add_task(struct damon_ctx *ctx, struct damon_task *t)
{
	list_add_tail(&t->list, &ctx->tasks_list);
}

static void damon_del_task(struct damon_task *t)
{
	list_del(&t->list);
}

static void damon_free_task(struct damon_task *t)
{
	struct damon_region *r, *next;

	damon_for_each_region_safe(r, next, t)
		damon_free_region(r);
	kfree(t);
}

static void damon_destroy_task(struct damon_task *t)
{
	damon_del_task(t);
	damon_free_task(t);
}

static unsigned int nr_damon_tasks(struct damon_ctx *ctx)
{
	struct damon_task *t;
	unsigned int nr_tasks = 0;

	damon_for_each_task(t, ctx)
		nr_tasks++;

	return nr_tasks;
}

static unsigned int nr_damon_regions(struct damon_task *t)
{
	struct damon_region *r;
	unsigned int nr_regions = 0;

	damon_for_each_region(r, t)
		nr_regions++;

	return nr_regions;
}

/*
 * Functions for the initialization
 */

static int __init damon_init(void)
{
	return 0;
}

module_init(damon_init);
