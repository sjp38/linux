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
 * - Functions for the initial monitoring target regions construction
 * - Functions for the dynamic monitoring target regions update
 * - Functions for the access checking of the regions
 * - Functions for the target validity check
 * - Functions for DAMON core logics and features
 * - Functions for the DAMON programming interface
 * - Functions for the DAMON debugfs interface
 * - Functions for the initialization
 */

#define pr_fmt(fmt) "damon: " fmt

#define CREATE_TRACE_POINTS

#include <asm-generic/mman-common.h>
#include <linux/damon.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/mmu_notifier.h>
#include <linux/module.h>
#include <linux/page_idle.h>
#include <linux/random.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
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

#define damon_for_each_scheme(s, ctx) \
	list_for_each_entry(s, &(ctx)->schemes_list, list)

#define damon_for_each_scheme_safe(s, next, ctx) \
	list_for_each_entry_safe(s, next, &(ctx)->schemes_list, list)

#define MIN_RECORD_BUFFER_LEN	1024
#define MAX_RECORD_BUFFER_LEN	(4 * 1024 * 1024)
#define MAX_RFILE_PATH_LEN	256

/* Get a random number in [l, r) */
#define damon_rand(l, r) (l + prandom_u32() % (r - l))

/* A monitoring context for debugfs interface users. */
static struct damon_ctx damon_user_ctx = {
	.sample_interval = 5 * 1000,
	.aggr_interval = 100 * 1000,
	.regions_update_interval = 1000 * 1000,
	.min_nr_regions = 10,
	.max_nr_regions = 1000,

	.init_target_regions = kdamond_init_vm_regions,
	.update_target_regions = kdamond_update_vm_regions,
	.prepare_access_checks = kdamond_prepare_vm_access_checks,
	.check_accesses = kdamond_check_vm_accesses,
	.target_valid = kdamond_vm_target_valid,
};

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

	region->age = 0;
	region->last_nr_accesses = 0;

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

static struct damos *damon_new_scheme(
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

static void damon_add_scheme(struct damon_ctx *ctx, struct damos *s)
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

static void damon_destroy_scheme(struct damos *s)
{
	damon_del_scheme(s);
	damon_free_scheme(s);
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
 * Functions for the initial monitoring target regions construction
 */

#define damon_get_task_struct(t) \
	        (get_pid_task(find_vpid((int)t->id), PIDTYPE_PID))

/*
 * Get the mm_struct of the given target
 *
 * '->id' of the target should be the relevant pid.  Currently, the reference
 * implementation of the low level primitives for the virtual address spaces
 * and the DAMON-based schemes execution logic has the assumption and uses
 * this.
 *
 * Caller _must_ put the mm_struct after use, unless it is NULL.
 *
 * Returns the mm_struct of the target on success, NULL on failure
 */
static struct mm_struct *damon_get_mm(struct damon_target *t)
{
	struct task_struct *task;
	struct mm_struct *mm;

	task = damon_get_task_struct(t);
	if (!task)
		return NULL;

	mm = get_task_mm(task);
	put_task_struct(task);
	return mm;
}

/*
 * Size-evenly split a region into 'nr_pieces' small regions
 *
 * Returns 0 on success, or negative error code otherwise.
 */
static int damon_split_region_evenly(struct damon_ctx *ctx,
		struct damon_region *r, unsigned int nr_pieces)
{
	unsigned long sz_orig, sz_piece, orig_end;
	struct damon_region *n = NULL, *next;
	unsigned long start;

	if (!r || !nr_pieces)
		return -EINVAL;

	orig_end = r->ar.end;
	sz_orig = r->ar.end - r->ar.start;
	sz_piece = ALIGN_DOWN(sz_orig / nr_pieces, MIN_REGION);

	if (!sz_piece)
		return -EINVAL;

	r->ar.end = r->ar.start + sz_piece;
	next = damon_next_region(r);
	for (start = r->ar.end; start + sz_piece <= orig_end;
			start += sz_piece) {
		n = damon_new_region(start, start + sz_piece);
		if (!n)
			return -ENOMEM;
		damon_insert_region(n, r, next);
		r = n;
	}
	/* complement last region for possible rounding error */
	if (n)
		n->ar.end = orig_end;

	return 0;
}

static unsigned long sz_range(struct damon_addr_range *r)
{
	return r->end - r->start;
}

static void swap_ranges(struct damon_addr_range *r1,
			struct damon_addr_range *r2)
{
	struct damon_addr_range tmp;

	tmp = *r1;
	*r1 = *r2;
	*r2 = tmp;
}

/*
 * Find three regions separated by two biggest unmapped regions
 *
 * vma		the head vma of the target address space
 * regions	an array of three address ranges that results will be saved
 *
 * This function receives an address space and finds three regions in it which
 * separated by the two biggest unmapped regions in the space.  Please refer to
 * below comments of 'damon_init_vm_regions_of()' function to know why this is
 * necessary.
 *
 * Returns 0 if success, or negative error code otherwise.
 */
static int damon_three_regions_in_vmas(struct vm_area_struct *vma,
				       struct damon_addr_range regions[3])
{
	struct damon_addr_range gap = {0}, first_gap = {0}, second_gap = {0};
	struct vm_area_struct *last_vma = NULL;
	unsigned long start = 0;
	struct rb_root rbroot;

	/* Find two biggest gaps so that first_gap > second_gap > others */
	for (; vma; vma = vma->vm_next) {
		if (!last_vma) {
			start = vma->vm_start;
			goto next;
		}

		if (vma->rb_subtree_gap <= sz_range(&second_gap)) {
			rbroot.rb_node = &vma->vm_rb;
			vma = rb_entry(rb_last(&rbroot),
					struct vm_area_struct, vm_rb);
			goto next;
		}

		gap.start = last_vma->vm_end;
		gap.end = vma->vm_start;
		if (sz_range(&gap) > sz_range(&second_gap)) {
			swap_ranges(&gap, &second_gap);
			if (sz_range(&second_gap) > sz_range(&first_gap))
				swap_ranges(&second_gap, &first_gap);
		}
next:
		last_vma = vma;
	}

	if (!sz_range(&second_gap) || !sz_range(&first_gap))
		return -EINVAL;

	/* Sort the two biggest gaps by address */
	if (first_gap.start > second_gap.start)
		swap_ranges(&first_gap, &second_gap);

	/* Store the result */
	regions[0].start = ALIGN(start, MIN_REGION);
	regions[0].end = ALIGN(first_gap.start, MIN_REGION);
	regions[1].start = ALIGN(first_gap.end, MIN_REGION);
	regions[1].end = ALIGN(second_gap.start, MIN_REGION);
	regions[2].start = ALIGN(second_gap.end, MIN_REGION);
	regions[2].end = ALIGN(last_vma->vm_end, MIN_REGION);

	return 0;
}

/*
 * Get the three regions in the given target (task)
 *
 * Returns 0 on success, negative error code otherwise.
 */
static int damon_three_regions_of(struct damon_target *t,
				struct damon_addr_range regions[3])
{
	struct mm_struct *mm;
	int rc;

	mm = damon_get_mm(t);
	if (!mm)
		return -EINVAL;

	down_read(&mm->mmap_sem);
	rc = damon_three_regions_in_vmas(mm->mmap, regions);
	up_read(&mm->mmap_sem);

	mmput(mm);
	return rc;
}

/*
 * Initialize the monitoring target regions for the given target (task)
 *
 * t	the given target
 *
 * Because only a number of small portions of the entire address space
 * is actually mapped to the memory and accessed, monitoring the unmapped
 * regions is wasteful.  That said, because we can deal with small noises,
 * tracking every mapping is not strictly required but could even incur a high
 * overhead if the mapping frequently changes or the number of mappings is
 * high.  The adaptive regions adjustment mechanism will further help to deal
 * with the noise by simply identifying the unmapped areas as a region that
 * has no access.  Moreover, applying the real mappings that would have many
 * unmapped areas inside will make the adaptive mechanism quite complex.  That
 * said, too huge unmapped areas inside the monitoring target should be removed
 * to not take the time for the adaptive mechanism.
 *
 * For the reason, we convert the complex mappings to three distinct regions
 * that cover every mapped area of the address space.  Also the two gaps
 * between the three regions are the two biggest unmapped areas in the given
 * address space.  In detail, this function first identifies the start and the
 * end of the mappings and the two biggest unmapped areas of the address space.
 * Then, it constructs the three regions as below:
 *
 *     [mappings[0]->start, big_two_unmapped_areas[0]->start)
 *     [big_two_unmapped_areas[0]->end, big_two_unmapped_areas[1]->start)
 *     [big_two_unmapped_areas[1]->end, mappings[nr_mappings - 1]->end)
 *
 * As usual memory map of processes is as below, the gap between the heap and
 * the uppermost mmap()-ed region, and the gap between the lowermost mmap()-ed
 * region and the stack will be two biggest unmapped regions.  Because these
 * gaps are exceptionally huge areas in usual address space, excluding these
 * two biggest unmapped regions will be sufficient to make a trade-off.
 *
 *   <heap>
 *   <BIG UNMAPPED REGION 1>
 *   <uppermost mmap()-ed region>
 *   (other mmap()-ed regions and small unmapped regions)
 *   <lowermost mmap()-ed region>
 *   <BIG UNMAPPED REGION 2>
 *   <stack>
 */
static void damon_init_vm_regions_of(struct damon_ctx *c,
				     struct damon_target *t)
{
	struct damon_region *r;
	struct damon_addr_range regions[3];
	unsigned long sz = 0, nr_pieces;
	int i;

	if (damon_three_regions_of(t, regions)) {
		pr_err("Failed to get three regions of target %lu\n", t->id);
		return;
	}

	for (i = 0; i < 3; i++)
		sz += regions[i].end - regions[i].start;
	if (c->min_nr_regions)
		sz /= c->min_nr_regions;
	if (sz < MIN_REGION)
		sz = MIN_REGION;

	/* Set the initial three regions of the target */
	for (i = 0; i < 3; i++) {
		r = damon_new_region(regions[i].start, regions[i].end);
		if (!r) {
			pr_err("%d'th init region creation failed\n", i);
			return;
		}
		damon_add_region(r, t);

		nr_pieces = (regions[i].end - regions[i].start) / sz;
		damon_split_region_evenly(c, r, nr_pieces);
	}
}

/* Initialize '->regions_list' of every target (task) */
void kdamond_init_vm_regions(struct damon_ctx *ctx)
{
	struct damon_target *t;

	damon_for_each_target(t, ctx) {
		/* the user may set the target regions as they want */
		if (!nr_damon_regions(t))
			damon_init_vm_regions_of(ctx, t);
	}
}

/*
 * Functions for the dynamic monitoring target regions update
 */

/*
 * Check whether a region is intersecting an address range
 *
 * Returns true if it is.
 */
static bool damon_intersect(struct damon_region *r, struct damon_addr_range *re)
{
	return !(r->ar.end <= re->start || re->end <= r->ar.start);
}

/*
 * Update damon regions for the three big regions of the given target
 *
 * t		the given target
 * bregions	the three big regions of the target
 */
static void damon_apply_three_regions(struct damon_ctx *ctx,
		struct damon_target *t, struct damon_addr_range bregions[3])
{
	struct damon_region *r, *next;
	unsigned int i = 0;

	/* Remove regions which are not in the three big regions now */
	damon_for_each_region_safe(r, next, t) {
		for (i = 0; i < 3; i++) {
			if (damon_intersect(r, &bregions[i]))
				break;
		}
		if (i == 3)
			damon_destroy_region(r);
	}

	/* Adjust intersecting regions to fit with the three big regions */
	for (i = 0; i < 3; i++) {
		struct damon_region *first = NULL, *last;
		struct damon_region *newr;
		struct damon_addr_range *br;

		br = &bregions[i];
		/* Get the first and last regions which intersects with br */
		damon_for_each_region(r, t) {
			if (damon_intersect(r, br)) {
				if (!first)
					first = r;
				last = r;
			}
			if (r->ar.start >= br->end)
				break;
		}
		if (!first) {
			/* no damon_region intersects with this big region */
			newr = damon_new_region(
					ALIGN_DOWN(br->start, MIN_REGION),
					ALIGN(br->end, MIN_REGION));
			if (!newr)
				continue;
			damon_insert_region(newr, damon_prev_region(r), r);
		} else {
			first->ar.start = ALIGN_DOWN(br->start, MIN_REGION);
			last->ar.end = ALIGN(br->end, MIN_REGION);
		}
	}
}

/*
 * Update regions for current memory mappings
 */
void kdamond_update_vm_regions(struct damon_ctx *ctx)
{
	struct damon_addr_range three_regions[3];
	struct damon_target *t;

	damon_for_each_target(t, ctx) {
		if (damon_three_regions_of(t, three_regions))
			continue;
		damon_apply_three_regions(ctx, t, three_regions);
	}
}

/*
 * Functions for the access checking of the regions
 */

static void damon_ptep_mkold(pte_t *pte, struct mm_struct *mm,
			     unsigned long addr)
{
	bool referenced = false;

	if (pte_young(*pte)) {
		referenced = true;
		*pte = pte_mkold(*pte);
	}

#ifdef CONFIG_MMU_NOTIFIER
	if (mmu_notifier_clear_young(mm, addr, addr + PAGE_SIZE))
		referenced = true;
#endif /* CONFIG_MMU_NOTIFIER */

	if (referenced) {
		clear_page_idle(pte_page(*pte));
		set_page_young(pte_page(*pte));
	}
}

static void damon_pmdp_mkold(pmd_t *pmd, struct mm_struct *mm,
			     unsigned long addr)
{
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	bool referenced = false;

	if (pmd_young(*pmd)) {
		referenced = true;
		*pmd = pmd_mkold(*pmd);
	}

#ifdef CONFIG_MMU_NOTIFIER
	if (mmu_notifier_clear_young(mm, addr,
				addr + ((1UL) << HPAGE_PMD_SHIFT)))
		referenced = true;
#endif /* CONFIG_MMU_NOTIFIER */

	if (referenced) {
		clear_page_idle(pmd_page(*pmd));
		set_page_young(pmd_page(*pmd));
	}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */
}

static void damon_mkold(struct mm_struct *mm, unsigned long addr)
{
	pte_t *pte = NULL;
	pmd_t *pmd = NULL;
	spinlock_t *ptl;

	if (follow_pte_pmd(mm, addr, NULL, &pte, &pmd, &ptl))
		return;

	if (pte) {
		damon_ptep_mkold(pte, mm, addr);
		pte_unmap_unlock(pte, ptl);
	} else {
		damon_pmdp_mkold(pmd, mm, addr);
		spin_unlock(ptl);
	}
}

static void damon_prepare_vm_access_check(struct damon_ctx *ctx,
			struct mm_struct *mm, struct damon_region *r)
{
	r->sampling_addr = damon_rand(r->ar.start, r->ar.end);

	damon_mkold(mm, r->sampling_addr);
}

void kdamond_prepare_vm_access_checks(struct damon_ctx *ctx)
{
	struct damon_target *t;
	struct mm_struct *mm;
	struct damon_region *r;

	damon_for_each_target(t, ctx) {
		mm = damon_get_mm(t);
		if (!mm)
			continue;
		damon_for_each_region(r, t)
			damon_prepare_vm_access_check(ctx, mm, r);
		mmput(mm);
	}
}

static bool damon_young(struct mm_struct *mm, unsigned long addr,
			unsigned long *page_sz)
{
	pte_t *pte = NULL;
	pmd_t *pmd = NULL;
	spinlock_t *ptl;
	bool young = false;

	if (follow_pte_pmd(mm, addr, NULL, &pte, &pmd, &ptl))
		return false;

	*page_sz = PAGE_SIZE;
	if (pte) {
		young = pte_young(*pte);
		pte_unmap_unlock(pte, ptl);
		return young;
	}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	young = pmd_young(*pmd);
	spin_unlock(ptl);
	*page_sz = ((1UL) << HPAGE_PMD_SHIFT);
#endif	/* CONFIG_TRANSPARENT_HUGEPAGE */

	return young;
}

/*
 * Check whether the region was accessed after the last preparation
 *
 * mm	'mm_struct' for the given virtual address space
 * r	the region to be checked
 */
static void damon_check_vm_access(struct damon_ctx *ctx,
			       struct mm_struct *mm, struct damon_region *r)
{
	static struct mm_struct *last_mm;
	static unsigned long last_addr;
	static unsigned long last_page_sz = PAGE_SIZE;
	static bool last_accessed;

	/* If the region is in the last checked page, reuse the result */
	if (mm == last_mm && (ALIGN_DOWN(last_addr, last_page_sz) ==
				ALIGN_DOWN(r->sampling_addr, last_page_sz))) {
		if (last_accessed)
			r->nr_accesses++;
		return;
	}

	last_accessed = damon_young(mm, r->sampling_addr, &last_page_sz);
	if (last_accessed)
		r->nr_accesses++;

	last_mm = mm;
	last_addr = r->sampling_addr;
}

unsigned int kdamond_check_vm_accesses(struct damon_ctx *ctx)
{
	struct damon_target *t;
	struct mm_struct *mm;
	struct damon_region *r;
	unsigned int max_nr_accesses = 0;

	damon_for_each_target(t, ctx) {
		mm = damon_get_mm(t);
		if (!mm)
			continue;
		damon_for_each_region(r, t) {
			damon_check_vm_access(ctx, mm, r);
			max_nr_accesses = max(r->nr_accesses, max_nr_accesses);
		}
		mmput(mm);
	}

	return max_nr_accesses;
}


/*
 * Functions for the target validity check
 */

bool kdamond_vm_target_valid(struct damon_target *t)
{
	struct task_struct *task;

	task = damon_get_task_struct(t);
	if (task) {
		put_task_struct(task);
		return true;
	}

	return false;
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

	rfile = filp_open(ctx->rfile_path, O_CREAT | O_RDWR | O_APPEND, 0644);
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
 * Functions for the DAMON debugfs interface
 */

static ssize_t debugfs_monitor_on_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = &damon_user_ctx;
	char monitor_on_buf[5];
	bool monitor_on;
	int len;

	monitor_on = damon_kdamond_running(ctx);
	len = snprintf(monitor_on_buf, 5, monitor_on ? "on\n" : "off\n");

	return simple_read_from_buffer(buf, count, ppos, monitor_on_buf, len);
}

/*
 * Returns non-empty string on success, negarive error code otherwise.
 */
static char *user_input_str(const char __user *buf, size_t count, loff_t *ppos)
{
	char *kbuf;
	ssize_t ret;

	/* We do not accept continuous write */
	if (*ppos)
		return ERR_PTR(-EINVAL);

	kbuf = kmalloc(count + 1, GFP_KERNEL);
	if (!kbuf)
		return ERR_PTR(-ENOMEM);

	ret = simple_write_to_buffer(kbuf, count + 1, ppos, buf, count);
	if (ret != count) {
		kfree(kbuf);
		return ERR_PTR(-EIO);
	}
	kbuf[ret] = '\0';

	return kbuf;
}

static ssize_t debugfs_monitor_on_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = &damon_user_ctx;
	ssize_t ret = count;
	char *kbuf;
	int err;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	/* Remove white space */
	if (sscanf(kbuf, "%s", kbuf) != 1)
		return -EINVAL;
	if (!strncmp(kbuf, "on", count))
		err = damon_start(ctx);
	else if (!strncmp(kbuf, "off", count))
		err = damon_stop(ctx);
	else
		return -EINVAL;

	if (err)
		ret = err;
	return ret;
}

static ssize_t sprint_schemes(struct damon_ctx *c, char *buf, ssize_t len)
{
	struct damos *s;
	int written = 0;
	int rc;

	damon_for_each_scheme(s, c) {
		rc = snprintf(&buf[written], len - written,
				"%lu %lu %u %u %u %u %d %lu %lu\n",
				s->min_sz_region, s->max_sz_region,
				s->min_nr_accesses, s->max_nr_accesses,
				s->min_age_region, s->max_age_region,
				s->action, s->stat_count, s->stat_sz);
		if (!rc)
			return -ENOMEM;

		written += rc;
	}
	return written;
}

static ssize_t debugfs_schemes_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = &damon_user_ctx;
	char *kbuf;
	ssize_t len;

	kbuf = kmalloc(count, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	mutex_lock(&ctx->kdamond_lock);
	len = sprint_schemes(ctx, kbuf, count);
	mutex_unlock(&ctx->kdamond_lock);
	if (len < 0)
		goto out;
	len = simple_read_from_buffer(buf, count, ppos, kbuf, len);

out:
	kfree(kbuf);
	return len;
}

static void free_schemes_arr(struct damos **schemes, ssize_t nr_schemes)
{
	ssize_t i;

	for (i = 0; i < nr_schemes; i++)
		kfree(schemes[i]);
	kfree(schemes);
}

static bool damos_action_valid(int action)
{
	switch (action) {
	case DAMOS_WILLNEED:
	case DAMOS_COLD:
	case DAMOS_PAGEOUT:
	case DAMOS_HUGEPAGE:
	case DAMOS_NOHUGEPAGE:
	case DAMOS_STAT:
		return true;
	default:
		return false;
	}
}

/*
 * Converts a string into an array of struct damos pointers
 *
 * Returns an array of struct damos pointers that converted if the conversion
 * success, or NULL otherwise.
 */
static struct damos **str_to_schemes(const char *str, ssize_t len,
				ssize_t *nr_schemes)
{
	struct damos *scheme, **schemes;
	const int max_nr_schemes = 256;
	int pos = 0, parsed, ret;
	unsigned long min_sz, max_sz;
	unsigned int min_nr_a, max_nr_a, min_age, max_age;
	unsigned int action;

	schemes = kmalloc_array(max_nr_schemes, sizeof(scheme),
			GFP_KERNEL);
	if (!schemes)
		return NULL;

	*nr_schemes = 0;
	while (pos < len && *nr_schemes < max_nr_schemes) {
		ret = sscanf(&str[pos], "%lu %lu %u %u %u %u %u%n",
				&min_sz, &max_sz, &min_nr_a, &max_nr_a,
				&min_age, &max_age, &action, &parsed);
		if (ret != 7)
			break;
		if (!damos_action_valid(action)) {
			pr_err("wrong action %d\n", action);
			goto fail;
		}

		pos += parsed;
		scheme = damon_new_scheme(min_sz, max_sz, min_nr_a, max_nr_a,
				min_age, max_age, action);
		if (!scheme)
			goto fail;

		schemes[*nr_schemes] = scheme;
		*nr_schemes += 1;
	}
	return schemes;
fail:
	free_schemes_arr(schemes, *nr_schemes);
	return NULL;
}

static ssize_t debugfs_schemes_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = &damon_user_ctx;
	char *kbuf;
	struct damos **schemes;
	ssize_t nr_schemes = 0, ret = count;
	int err;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	schemes = str_to_schemes(kbuf, ret, &nr_schemes);
	if (!schemes) {
		ret = -EINVAL;
		goto out;
	}

	mutex_lock(&ctx->kdamond_lock);
	if (ctx->kdamond) {
		ret = -EBUSY;
		goto unlock_out;
	}

	err = damon_set_schemes(ctx, schemes, nr_schemes);
	if (err)
		ret = err;
	else
		nr_schemes = 0;
unlock_out:
	mutex_unlock(&ctx->kdamond_lock);
	free_schemes_arr(schemes, nr_schemes);
out:
	kfree(kbuf);
	return ret;
}

static ssize_t sprint_target_ids(struct damon_ctx *ctx, char *buf, ssize_t len)
{
	struct damon_target *t;
	int written = 0;
	int rc;

	damon_for_each_target(t, ctx) {
		rc = snprintf(&buf[written], len - written, "%lu ", t->id);
		if (!rc)
			return -ENOMEM;
		written += rc;
	}
	if (written)
		written -= 1;
	written += snprintf(&buf[written], len - written, "\n");
	return written;
}

static ssize_t debugfs_target_ids_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = &damon_user_ctx;
	ssize_t len;
	char ids_buf[320];

	mutex_lock(&ctx->kdamond_lock);
	len = sprint_target_ids(ctx, ids_buf, 320);
	mutex_unlock(&ctx->kdamond_lock);
	if (len < 0)
		return len;

	return simple_read_from_buffer(buf, count, ppos, ids_buf, len);
}

/*
 * Converts a string into an array of unsigned long integers
 *
 * Returns an array of unsigned long integers if the conversion success, or
 * NULL otherwise.
 */
static unsigned long *str_to_target_ids(const char *str, ssize_t len,
					ssize_t *nr_ids)
{
	unsigned long *ids;
	const int max_nr_ids = 32;
	unsigned long id;
	int pos = 0, parsed, ret;

	*nr_ids = 0;
	ids = kmalloc_array(max_nr_ids, sizeof(id), GFP_KERNEL);
	if (!ids)
		return NULL;
	while (*nr_ids < max_nr_ids && pos < len) {
		ret = sscanf(&str[pos], "%lu%n", &id, &parsed);
		pos += parsed;
		if (ret != 1)
			break;
		ids[*nr_ids] = id;
		*nr_ids += 1;
	}

	return ids;
}

static ssize_t debugfs_target_ids_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = &damon_user_ctx;
	char *kbuf;
	unsigned long *targets;
	ssize_t nr_targets;
	ssize_t ret = count;
	int err;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	targets = str_to_target_ids(kbuf, ret, &nr_targets);
	if (!targets) {
		ret = -ENOMEM;
		goto out;
	}

	mutex_lock(&ctx->kdamond_lock);
	if (ctx->kdamond) {
		ret = -EINVAL;
		goto unlock_out;
	}

	err = damon_set_targets(ctx, targets, nr_targets);
	if (err)
		ret = err;
unlock_out:
	mutex_unlock(&ctx->kdamond_lock);
	kfree(targets);
out:
	kfree(kbuf);
	return ret;
}

static ssize_t debugfs_record_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = &damon_user_ctx;
	char record_buf[20 + MAX_RFILE_PATH_LEN];
	int ret;

	mutex_lock(&ctx->kdamond_lock);
	ret = snprintf(record_buf, ARRAY_SIZE(record_buf), "%u %s\n",
			ctx->rbuf_len, ctx->rfile_path);
	mutex_unlock(&ctx->kdamond_lock);
	return simple_read_from_buffer(buf, count, ppos, record_buf, ret);
}

static ssize_t debugfs_record_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = &damon_user_ctx;
	char *kbuf;
	unsigned int rbuf_len;
	char rfile_path[MAX_RFILE_PATH_LEN];
	ssize_t ret = count;
	int err;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	if (sscanf(kbuf, "%u %s",
				&rbuf_len, rfile_path) != 2) {
		ret = -EINVAL;
		goto out;
	}

	mutex_lock(&ctx->kdamond_lock);
	if (ctx->kdamond) {
		ret = -EBUSY;
		goto unlock_out;
	}

	err = damon_set_recording(ctx, rbuf_len, rfile_path);
	if (err)
		ret = err;
unlock_out:
	mutex_unlock(&ctx->kdamond_lock);
out:
	kfree(kbuf);
	return ret;
}

static ssize_t sprint_init_regions(struct damon_ctx *c, char *buf, ssize_t len)
{
	struct damon_target *t;
	struct damon_region *r;
	int written = 0;
	int rc;

	damon_for_each_target(t, c) {
		damon_for_each_region(r, t) {
			rc = snprintf(&buf[written], len - written,
					"%lu %lu %lu\n",
					t->id, r->ar.start, r->ar.end);
			if (!rc)
				return -ENOMEM;
			written += rc;
		}
	}
	return written;
}

static ssize_t debugfs_init_regions_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = &damon_user_ctx;
	char *kbuf;
	ssize_t len;

	kbuf = kmalloc(count, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	mutex_lock(&ctx->kdamond_lock);
	if (ctx->kdamond) {
		mutex_unlock(&ctx->kdamond_lock);
		return -EBUSY;
	}

	len = sprint_init_regions(ctx, kbuf, count);
	mutex_unlock(&ctx->kdamond_lock);
	if (len < 0)
		goto out;
	len = simple_read_from_buffer(buf, count, ppos, kbuf, len);

out:
	kfree(kbuf);
	return len;
}

static int add_init_region(struct damon_ctx *c,
			 unsigned long target_id, struct damon_addr_range *ar)
{
	struct damon_target *t;
	struct damon_region *r, *prev;
	int rc = -EINVAL;

	if (ar->start >= ar->end)
		return -EINVAL;

	damon_for_each_target(t, c) {
		if (t->id == target_id) {
			r = damon_new_region(ar->start, ar->end);
			if (!r)
				return -ENOMEM;
			damon_add_region(r, t);
			if (nr_damon_regions(t) > 1) {
				prev = damon_prev_region(r);
				if (prev->ar.end > r->ar.start) {
					damon_destroy_region(r);
					return -EINVAL;
				}
			}
			rc = 0;
		}
	}
	return rc;
}

static int set_init_regions(struct damon_ctx *c, const char *str, ssize_t len)
{
	struct damon_target *t;
	struct damon_region *r, *next;
	int pos = 0, parsed, ret;
	unsigned long target_id;
	struct damon_addr_range ar;
	int err;

	damon_for_each_target(t, c) {
		damon_for_each_region_safe(r, next, t)
			damon_destroy_region(r);
	}

	while (pos < len) {
		ret = sscanf(&str[pos], "%lu %lu %lu%n",
				&target_id, &ar.start, &ar.end, &parsed);
		if (ret != 3)
			break;
		err = add_init_region(c, target_id, &ar);
		if (err)
			goto fail;
		pos += parsed;
	}

	return 0;

fail:
	damon_for_each_target(t, c) {
		damon_for_each_region_safe(r, next, t)
			damon_destroy_region(r);
	}
	return err;
}

static ssize_t debugfs_init_regions_write(struct file *file,
					  const char __user *buf, size_t count,
					  loff_t *ppos)
{
	struct damon_ctx *ctx = &damon_user_ctx;
	char *kbuf;
	ssize_t ret = count;
	int err;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	mutex_lock(&ctx->kdamond_lock);
	if (ctx->kdamond) {
		ret = -EBUSY;
		goto unlock_out;
	}

	err = set_init_regions(ctx, kbuf, ret);
	if (err)
		ret = err;

unlock_out:
	mutex_unlock(&ctx->kdamond_lock);
	kfree(kbuf);
	return ret;
}

static ssize_t debugfs_attrs_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = &damon_user_ctx;
	char kbuf[128];
	int ret;

	mutex_lock(&ctx->kdamond_lock);
	ret = snprintf(kbuf, ARRAY_SIZE(kbuf), "%lu %lu %lu %lu %lu\n",
			ctx->sample_interval, ctx->aggr_interval,
			ctx->regions_update_interval, ctx->min_nr_regions,
			ctx->max_nr_regions);
	mutex_unlock(&ctx->kdamond_lock);

	return simple_read_from_buffer(buf, count, ppos, kbuf, ret);
}

static ssize_t debugfs_attrs_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = &damon_user_ctx;
	unsigned long s, a, r, minr, maxr;
	char *kbuf;
	ssize_t ret = count;
	int err;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	if (sscanf(kbuf, "%lu %lu %lu %lu %lu",
				&s, &a, &r, &minr, &maxr) != 5) {
		ret = -EINVAL;
		goto out;
	}

	mutex_lock(&ctx->kdamond_lock);
	if (ctx->kdamond) {
		ret = -EBUSY;
		goto unlock_out;
	}

	err = damon_set_attrs(ctx, s, a, r, minr, maxr);
	if (err)
		ret = err;
unlock_out:
	mutex_unlock(&ctx->kdamond_lock);
out:
	kfree(kbuf);
	return ret;
}

static const struct file_operations monitor_on_fops = {
	.owner = THIS_MODULE,
	.read = debugfs_monitor_on_read,
	.write = debugfs_monitor_on_write,
};

static const struct file_operations target_ids_fops = {
	.owner = THIS_MODULE,
	.read = debugfs_target_ids_read,
	.write = debugfs_target_ids_write,
};

static const struct file_operations schemes_fops = {
	.owner = THIS_MODULE,
	.read = debugfs_schemes_read,
	.write = debugfs_schemes_write,
};

static const struct file_operations record_fops = {
	.owner = THIS_MODULE,
	.read = debugfs_record_read,
	.write = debugfs_record_write,
};

static const struct file_operations init_regions_fops = {
	.owner = THIS_MODULE,
	.read = debugfs_init_regions_read,
	.write = debugfs_init_regions_write,
};

static const struct file_operations attrs_fops = {
	.owner = THIS_MODULE,
	.read = debugfs_attrs_read,
	.write = debugfs_attrs_write,
};

static struct dentry *debugfs_root;

static int __init damon_debugfs_init(void)
{
	const char * const file_names[] = {"attrs", "init_regions", "record",
		"schemes", "target_ids", "monitor_on"};
	const struct file_operations *fops[] = {&attrs_fops,
		&init_regions_fops, &record_fops, &schemes_fops,
		&target_ids_fops, &monitor_on_fops};
	int i;

	debugfs_root = debugfs_create_dir("damon", NULL);
	if (!debugfs_root) {
		pr_err("failed to create the debugfs dir\n");
		return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(file_names); i++) {
		if (!debugfs_create_file(file_names[i], 0600, debugfs_root,
					NULL, fops[i])) {
			pr_err("failed to create %s file\n", file_names[i]);
			return -ENOMEM;
		}
	}

	return 0;
}

static int __init damon_init_user_ctx(void)
{
	int rc;

	struct damon_ctx *ctx = &damon_user_ctx;

	ktime_get_coarse_ts64(&ctx->last_aggregation);
	ctx->last_regions_update = ctx->last_aggregation;

	rc = damon_set_recording(ctx, 1024 * 1024, "/damon.data");
	if (rc)
		return rc;

	mutex_init(&ctx->kdamond_lock);

	INIT_LIST_HEAD(&ctx->targets_list);
	INIT_LIST_HEAD(&ctx->schemes_list);

	return 0;
}

/*
 * Functions for the initialization
 */

static int __init damon_init(void)
{
	int rc;

	rc = damon_init_user_ctx();
	if (rc)
		return rc;

	rc = damon_debugfs_init();
	if (rc)
		pr_err("%s: debugfs init failed\n", __func__);

	return rc;
}

module_init(damon_init);

#include "damon-test.h"
