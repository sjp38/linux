// SPDX-License-Identifier: GPL-2.0
/*
 * Data Access Monitor
 *
 * Copyright 2019-2020 Amazon.com, Inc. or its affiliates.
 *
 * Author: SeongJae Park <sjpark@amazon.de>
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

#define damon_for_each_task(ctx, t) \
	list_for_each_entry(t, &(ctx)->tasks_list, list)

#define damon_for_each_task_safe(ctx, t, next) \
	list_for_each_entry_safe(t, next, &(ctx)->tasks_list, list)

/* Get a random number in [l, r) */
#define damon_rand(l, r) (l + prandom_u32() % (r - l))

/*
 * Construct a damon_region struct
 *
 * Returns the pointer to the new struct if success, or NULL otherwise
 */
static struct damon_region *damon_new_region(struct damon_ctx *ctx,
				unsigned long vm_start, unsigned long vm_end)
{
	struct damon_region *region;

	region = kmalloc(sizeof(*region), GFP_KERNEL);
	if (!region)
		return NULL;

	region->vm_start = vm_start;
	region->vm_end = vm_end;
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

	damon_for_each_task(ctx, t)
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
 * Get the mm_struct of the given task
 *
 * Caller _must_ put the mm_struct after use, unless it is NULL.
 *
 * Returns the mm_struct of the task on success, NULL on failure
 */
static struct mm_struct *damon_get_mm(struct damon_task *t)
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

	orig_end = r->vm_end;
	sz_orig = r->vm_end - r->vm_start;
	sz_piece = ALIGN_DOWN(sz_orig / nr_pieces, MIN_REGION);

	if (!sz_piece)
		return -EINVAL;

	r->vm_end = r->vm_start + sz_piece;
	next = damon_next_region(r);
	for (start = r->vm_end; start + sz_piece <= orig_end;
			start += sz_piece) {
		n = damon_new_region(ctx, start, start + sz_piece);
		if (!n)
			return -ENOMEM;
		damon_insert_region(n, r, next);
		r = n;
	}
	/* complement last region for possible rounding error */
	if (n)
		n->vm_end = orig_end;

	return 0;
}

struct region {
	unsigned long start;
	unsigned long end;
};

static unsigned long sz_region(struct region *r)
{
	return r->end - r->start;
}

static void swap_regions(struct region *r1, struct region *r2)
{
	struct region tmp;

	tmp = *r1;
	*r1 = *r2;
	*r2 = tmp;
}

/*
 * Find three regions seperated by two biggest unmapped regions
 *
 * vma		the head vma of the target address space
 * regions	an array of three 'struct region's that results will be saved
 *
 * This function receives an address space and finds three regions in it which
 * separated by the two biggest unmapped regions in the space.  Please refer to
 * below comments of 'damon_init_regions_of()' function to know why this is
 * necessary.
 *
 * Returns 0 if success, or negative error code otherwise.
 */
static int damon_three_regions_in_vmas(struct vm_area_struct *vma,
		struct region regions[3])
{
	struct region gap = {0}, first_gap = {0}, second_gap = {0};
	struct vm_area_struct *last_vma = NULL;
	unsigned long start = 0;

	/* Find two biggest gaps so that first_gap > second_gap > others */
	for (; vma; vma = vma->vm_next) {
		if (!last_vma) {
			start = vma->vm_start;
			last_vma = vma;
			continue;
		}
		gap.start = last_vma->vm_end;
		gap.end = vma->vm_start;
		if (sz_region(&gap) > sz_region(&second_gap)) {
			swap_regions(&gap, &second_gap);
			if (sz_region(&second_gap) > sz_region(&first_gap))
				swap_regions(&second_gap, &first_gap);
		}
		last_vma = vma;
	}

	if (!sz_region(&second_gap) || !sz_region(&first_gap))
		return -EINVAL;

	/* Sort the two biggest gaps by address */
	if (first_gap.start > second_gap.start)
		swap_regions(&first_gap, &second_gap);

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
 * Get the three regions in the given task
 *
 * Returns 0 on success, negative error code otherwise.
 */
static int damon_three_regions_of(struct damon_task *t,
				struct region regions[3])
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
 * Initialize the monitoring target regions for the given task
 *
 * t	the given target task
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
static void damon_init_regions_of(struct damon_ctx *c, struct damon_task *t)
{
	struct damon_region *r, *m = NULL;
	struct region regions[3];
	int i;

	if (damon_three_regions_of(t, regions)) {
		pr_err("Failed to get three regions of task %d\n", t->pid);
		return;
	}

	/* Set the initial three regions of the task */
	for (i = 0; i < 3; i++) {
		r = damon_new_region(c, regions[i].start, regions[i].end);
		if (!r) {
			pr_err("%d'th init region creation failed\n", i);
			return;
		}
		damon_add_region(r, t);
		if (i == 1)
			m = r;
	}

	/* Split the middle region into 'min_nr_regions - 2' regions */
	if (damon_split_region_evenly(c, m, c->min_nr_regions - 2))
		pr_warn("Init middle region failed to be split\n");
}

/* Initialize '->regions_list' of every task */
static void kdamond_init_regions(struct damon_ctx *ctx)
{
	struct damon_task *t;

	damon_for_each_task(ctx, t)
		damon_init_regions_of(ctx, t);
}

static void damon_mkold(struct mm_struct *mm, unsigned long addr)
{
	pte_t *pte = NULL;
	pmd_t *pmd = NULL;
	spinlock_t *ptl;

	if (follow_pte_pmd(mm, addr, NULL, &pte, &pmd, &ptl))
		return;

	if (pte) {
		if (pte_young(*pte)) {
			clear_page_idle(pte_page(*pte));
			set_page_young(pte_page(*pte));
		}
		*pte = pte_mkold(*pte);
		pte_unmap_unlock(pte, ptl);
		return;
	}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	if (pmd_young(*pmd)) {
		clear_page_idle(pmd_page(*pmd));
		set_page_young(pmd_page(*pmd));
	}
	*pmd = pmd_mkold(*pmd);
	spin_unlock(ptl);
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */
}

static void damon_prepare_access_check(struct damon_ctx *ctx,
			struct mm_struct *mm, struct damon_region *r)
{
	r->sampling_addr = damon_rand(r->vm_start, r->vm_end);

	damon_mkold(mm, r->sampling_addr);
}

static void kdamond_prepare_access_checks(struct damon_ctx *ctx)
{
	struct damon_task *t;
	struct mm_struct *mm;
	struct damon_region *r;

	damon_for_each_task(ctx, t) {
		mm = damon_get_mm(t);
		if (!mm)
			continue;
		damon_for_each_region(r, t)
			damon_prepare_access_check(ctx, mm, r);
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
 * Check whether the region was accessed and prepare for next check
 *
 * mm	'mm_struct' for the given virtual address space
 * r	the region to be checked
 */
static void damon_check_access(struct damon_ctx *ctx,
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

static unsigned int kdamond_check_accesses(struct damon_ctx *ctx)
{
	struct damon_task *t;
	struct mm_struct *mm;
	struct damon_region *r;
	unsigned int max_nr_accesses = 0;

	damon_for_each_task(ctx, t) {
		mm = damon_get_mm(t);
		if (!mm)
			continue;
		damon_for_each_region(r, t) {
			damon_check_access(ctx, mm, r);
			max_nr_accesses = max(r->nr_accesses, max_nr_accesses);
		}

		mmput(mm);
	}
	return max_nr_accesses;
}

/**
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

	damon_for_each_task(c, t) {
		damon_for_each_region(r, t)
			r->nr_accesses = 0;
	}
}

#define sz_damon_region(r) (r->vm_end - r->vm_start)

/*
 * Merge two adjacent regions into one region
 */
static void damon_merge_two_regions(struct damon_region *l,
				struct damon_region *r)
{
	l->nr_accesses = (l->nr_accesses * sz_damon_region(l) +
			r->nr_accesses * sz_damon_region(r)) /
			(sz_damon_region(l) + sz_damon_region(r));
	l->vm_end = r->vm_end;
	damon_destroy_region(r);
}

#define diff_of(a, b) (a > b ? a - b : b - a)

/*
 * Merge adjacent regions having similar access frequencies
 *
 * t		task affected by merge operation
 * thres	'->nr_accesses' diff threshold for the merge
 */
static void damon_merge_regions_of(struct damon_task *t, unsigned int thres)
{
	struct damon_region *r, *prev = NULL, *next;

	damon_for_each_region_safe(r, next, t) {
		if (!prev || prev->vm_end != r->vm_start ||
		    diff_of(prev->nr_accesses, r->nr_accesses) > thres) {
			prev = r;
			continue;
		}
		damon_merge_two_regions(prev, r);
	}
}

/*
 * Merge adjacent regions having similar access frequencies
 *
 * threshold	merge regions having nr_accesses diff larger than this
 *
 * This function merges monitoring target regions which are adjacent and their
 * access frequencies are similar.  This is for minimizing the monitoring
 * overhead under the dynamically changeable access pattern.  If a merge was
 * unnecessarily made, later 'kdamond_split_regions()' will revert it.
 */
static void kdamond_merge_regions(struct damon_ctx *c, unsigned int threshold)
{
	struct damon_task *t;

	damon_for_each_task(c, t)
		damon_merge_regions_of(t, threshold);
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

	new = damon_new_region(ctx, r->vm_start + sz_r, r->vm_end);
	r->vm_end = new->vm_start;

	damon_insert_region(new, r, damon_next_region(r));
}

/* Split every region in the given task into two randomly-sized regions */
static void damon_split_regions_of(struct damon_ctx *ctx, struct damon_task *t)
{
	struct damon_region *r, *next;
	unsigned long sz_orig_region, sz_left_region;

	damon_for_each_region_safe(r, next, t) {
		sz_orig_region = r->vm_end - r->vm_start;

		/*
		 * Randomly select size of left sub-region to be at least
		 * 10 percent and at most 90% of original region
		 */
		sz_left_region = ALIGN_DOWN(damon_rand(1, 10) * sz_orig_region
					    / 10, MIN_REGION);
		/* Do not allow blank region */
		if (sz_left_region == 0 || sz_left_region >= sz_orig_region)
			continue;

		damon_split_region_at(ctx, r, sz_left_region);
	}
}

/*
 * splits every target region into two randomly-sized regions
 *
 * This function splits every target region into two random-sized regions if
 * current total number of the regions is equal or smaller than half of the
 * user-specified maximum number of regions.  This is for maximizing the
 * monitoring accuracy under the dynamically changeable access patterns.  If a
 * split was unnecessarily made, later 'kdamond_merge_regions()' will revert
 * it.
 */
static void kdamond_split_regions(struct damon_ctx *ctx)
{
	struct damon_task *t;
	unsigned int nr_regions = 0;

	damon_for_each_task(ctx, t)
		nr_regions += nr_damon_regions(t);
	if (nr_regions > ctx->max_nr_regions / 2)
		return;

	damon_for_each_task(ctx, t)
		damon_split_regions_of(ctx, t);
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

	stop = kthread_should_stop();
	if (stop)
		return true;

	damon_for_each_task(ctx, t) {
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
	unsigned int max_nr_accesses = 0;

	pr_info("kdamond (%d) starts\n", ctx->kdamond->pid);
	kdamond_init_regions(ctx);
	while (!kdamond_need_stop(ctx)) {
		kdamond_prepare_access_checks(ctx);

		usleep_range(ctx->sample_interval, ctx->sample_interval + 1);

		max_nr_accesses = kdamond_check_accesses(ctx);

		if (kdamond_aggregate_interval_passed(ctx)) {
			kdamond_merge_regions(ctx, max_nr_accesses / 10);
			kdamond_reset_aggregated(ctx);
			kdamond_split_regions(ctx);
		}
	}
	damon_for_each_task(ctx, t) {
		damon_for_each_region_safe(r, next, t)
			damon_destroy_region(r);
	}
	pr_debug("kdamond (%d) finishes\n", ctx->kdamond->pid);
	mutex_lock(&ctx->kdamond_lock);
	ctx->kdamond = NULL;
	mutex_unlock(&ctx->kdamond_lock);

	return 0;
}

/*
 * Controller functions
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
		ctx->kdamond = kthread_run(kdamond_fn, ctx, "kdamond");
		if (IS_ERR(ctx->kdamond))
			err = PTR_ERR(ctx->kdamond);
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
		kthread_stop(ctx->kdamond);
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

	damon_for_each_task_safe(ctx, t, next)
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
 * @min_nr_reg:		minimal number of regions
 * @max_nr_reg:		maximum number of regions
 *
 * This function should not be called while the kdamond is running.
 * Every time interval is in micro-seconds.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int damon_set_attrs(struct damon_ctx *ctx,
		    unsigned long sample_int, unsigned long aggr_int,
		    unsigned long min_nr_reg, unsigned long max_nr_reg)
{
	if (min_nr_reg < 3) {
		pr_err("min_nr_regions (%lu) must be at least 3\n",
				min_nr_reg);
		return -EINVAL;
	}
	if (min_nr_reg > ctx->max_nr_regions) {
		pr_err("invalid nr_regions.  min (%lu) > max (%lu)\n",
				min_nr_reg, max_nr_reg);
		return -EINVAL;
	}

	ctx->sample_interval = sample_int;
	ctx->aggr_interval = aggr_int;
	ctx->min_nr_regions = min_nr_reg;
	ctx->max_nr_regions = max_nr_reg;

	return 0;
}

static int __init damon_init(void)
{
	return 0;
}

static void __exit damon_exit(void)
{
	return;
}

module_init(damon_init);
module_exit(damon_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SeongJae Park <sjpark@amazon.de>");
MODULE_DESCRIPTION("DAMON: Data Access MONitor");
