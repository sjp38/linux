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
 * - Functions for the initialization
 */

#define pr_fmt(fmt) "damon: " fmt

#include <linux/damon.h>
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
 * Reset the aggregated monitoring results
 */
static void kdamond_reset_aggregated(struct damon_ctx *c)
{
	struct damon_target *t;
	struct damon_region *r;

	damon_for_each_target(t, c) {
		damon_for_each_region(r, t)
			r->nr_accesses = 0;
	}
}

#define sz_damon_region(r) (r->ar.end - r->ar.start)

/*
 * Merge two adjacent regions into one region
 */
static void damon_merge_two_regions(struct damon_region *l,
				struct damon_region *r)
{
	l->nr_accesses = (l->nr_accesses * sz_damon_region(l) +
			r->nr_accesses * sz_damon_region(r)) /
			(sz_damon_region(l) + sz_damon_region(r));
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
			kdamond_reset_aggregated(ctx);
			kdamond_split_regions(ctx);
		}

		if (kdamond_need_update_regions(ctx)) {
			if (ctx->update_target_regions)
				ctx->update_target_regions(ctx);
			sz_limit = damon_region_sz_limit(ctx);
		}
	}
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
