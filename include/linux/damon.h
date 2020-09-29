/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DAMON api
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 */

#ifndef _DAMON_H_
#define _DAMON_H_

#include <linux/types.h>

/**
 * struct damon_addr_range - Represents an address region of [@start, @end).
 * @start:	Start address of the region (inclusive).
 * @end:	End address of the region (exclusive).
 */
struct damon_addr_range {
	unsigned long start;
	unsigned long end;
};

/**
 * struct damon_region - Represents a monitoring target region.
 * @ar:			The address range of the region.
 * @nr_accesses:	Access frequency of this region.
 * @list:		List head for siblings.
 */
struct damon_region {
	struct damon_addr_range ar;
	unsigned int nr_accesses;
	struct list_head list;
};

/**
 * struct damon_target - Represents a monitoring target.
 * @id:			Unique identifier for this target.
 * @regions_list:	Head of the monitoring target regions of this target.
 * @list:		List head for siblings.
 *
 * Each monitoring context could have multiple targets.  For example, a context
 * for virtual memory address spaces could have multiple target processes.  The
 * @id of each target should be unique among the targets of the context.  For
 * example, in the virtual address monitoring context, it could be a pidfd or
 * an address of an mm_struct.
 */
struct damon_target {
	unsigned long id;
	struct list_head regions_list;
	struct list_head list;
};

/**
 * struct damon_ctx - Represents a context for each monitoring.
 * @targets_list:	Head of monitoring targets (&damon_target) list.
 */
struct damon_ctx {
	struct list_head targets_list;	/* 'damon_target' objects */
};

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

#ifdef CONFIG_DAMON

struct damon_region *damon_new_region(unsigned long start, unsigned long end);
inline void damon_insert_region(struct damon_region *r,
		struct damon_region *prev, struct damon_region *next);
void damon_add_region(struct damon_region *r, struct damon_target *t);
void damon_destroy_region(struct damon_region *r);

struct damon_target *damon_new_target(unsigned long id);
void damon_add_target(struct damon_ctx *ctx, struct damon_target *t);
void damon_free_target(struct damon_target *t);
void damon_destroy_target(struct damon_target *t);
unsigned int damon_nr_regions(struct damon_target *t);

#endif	/* CONFIG_DAMON */

#endif
