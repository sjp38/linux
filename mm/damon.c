// SPDX-License-Identifier: GPL-2.0
/*
 * Data Access Monitor
 *
 * Copyright 2019 Amazon.com, Inc. or its affiliates.  All rights reserved.
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 */

#define pr_fmt(fmt) "damon: " fmt

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/slab.h>

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

#define damon_for_each_task(t) \
	list_for_each_entry(t, &damon_tasks_list, list)

#define damon_for_each_task_safe(t, next) \
	list_for_each_entry_safe(t, next, &damon_tasks_list, list)

/* Represents a monitoring target region on the virtual address space */
struct damon_region {
	unsigned long vm_start;
	unsigned long vm_end;
	unsigned long sampling_addr;
	unsigned int nr_accesses;
	struct list_head list;
};

/* Represents a monitoring target task */
struct damon_task {
	unsigned long pid;
	struct list_head regions_list;
	struct list_head list;
};

/* List of damon_task objects */
static LIST_HEAD(damon_tasks_list);

static struct rnd_state rndseed;
/* Get a random number in [l, r) */
#define damon_rand(l, r) (l + prandom_u32_state(&rndseed) % (r - l))

/*
 * Construct a damon_region struct
 *
 * Returns the pointer to the new struct if success, or NULL otherwise
 */
static struct damon_region *damon_new_region(unsigned long vm_start,
					unsigned long vm_end)
{
	struct damon_region *ret;

	ret = kmalloc(sizeof(struct damon_region), GFP_KERNEL);
	if (!ret)
		return NULL;
	ret->vm_start = vm_start;
	ret->vm_end = vm_end;
	ret->nr_accesses = 0;
	ret->sampling_addr = damon_rand(vm_start, vm_end);
	INIT_LIST_HEAD(&ret->list);

	return ret;
}

/*
 * Add a region between two other regions
 */
static inline void damon_add_region(struct damon_region *r,
		struct damon_region *prev, struct damon_region *next)
{
	__list_add(&r->list, &prev->list, &next->list);
}

/*
 * Append a region to a task's list of regions
 */
static void damon_add_region_tail(struct damon_region *r, struct damon_task *t)
{
	list_add_tail(&r->list, &t->regions_list);
}

/*
 * Delete a region from its list
 */
static void damon_del_region(struct damon_region *r)
{
	list_del(&r->list);
}

/*
 * De-allocate a region
 */
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
static struct damon_task *damon_new_task(unsigned long pid)
{
	struct damon_task *t;

	t = kmalloc(sizeof(struct damon_task), GFP_KERNEL);
	if (!t)
		return NULL;
	t->pid = pid;
	INIT_LIST_HEAD(&t->regions_list);

	return t;
}

/* Returns n-th damon_region of the given task */
struct damon_region *damon_nth_region_of(struct damon_task *t, unsigned int n)
{
	struct damon_region *r;
	unsigned int i;

	i = 0;
	damon_for_each_region(r, t) {
		if (i++ == n)
			return r;
	}
	return NULL;
}

static void damon_add_task_tail(struct damon_task *t)
{
	list_add_tail(&t->list, &damon_tasks_list);
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

/*
 * Returns number of monitoring target tasks
 */
static unsigned int nr_damon_tasks(void)
{
	struct damon_task *t;
	unsigned int ret = 0;

	damon_for_each_task(t)
		ret++;
	return ret;
}

/*
 * Returns the number of target regions for a given target task
 */
static unsigned int nr_damon_regions(struct damon_task *t)
{
	struct damon_region *r;
	unsigned int ret = 0;

	damon_for_each_region(r, t)
		ret++;
	return ret;
}

static int __init damon_init(void)
{
	pr_info("init\n");

	prandom_seed_state(&rndseed, 42);
	return 0;
}

static void __exit damon_exit(void)
{
	pr_info("exit\n");
}

module_init(damon_init);
module_exit(damon_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SeongJae Park <sjpark@amazon.de>");
MODULE_DESCRIPTION("DAMON: Data Access MONitor");
