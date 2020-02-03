// SPDX-License-Identifier: GPL-2.0
/*
 * Data Access Monitor
 *
 * Copyright 2019 Amazon.com, Inc. or its affiliates.  All rights reserved.
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 */

#define pr_fmt(fmt) "damon: " fmt

#define CREATE_TRACE_POINTS

#include <linux/damon.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/page_idle.h>
#include <linux/random.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <trace/events/damon.h>

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

#define MAX_RFILE_PATH_LEN	256

/* Get a random number in [l, r) */
#define damon_rand(ctx, l, r) (l + prandom_u32_state(&ctx->rndseed) % (r - l))

/*
 * For each 'sample_interval', DAMON checks whether each region is accessed or
 * not.  It aggregates and keeps the access information (number of accesses to
 * each region) for 'aggr_interval' and then flushes it to the result buffer if
 * an 'aggr_interval' surpassed.  And for each 'regions_update_interval', damon
 * checks whether the memory mapping of the target tasks has changed (e.g., by
 * mmap() calls from the applications) and applies the changes.
 *
 * All time intervals are in micro-seconds.
 */
static struct damon_ctx damon_user_ctx = {
	.sample_interval = 5 * 1000,
	.aggr_interval = 100 * 1000,
	.regions_update_interval = 1000 * 1000,
	.min_nr_regions = 10,
	.max_nr_regions = 1000,
};

/*
 * Construct a damon_region struct
 *
 * Returns the pointer to the new struct if success, or NULL otherwise
 */
static struct damon_region *damon_new_region(struct damon_ctx *ctx,
				unsigned long vm_start, unsigned long vm_end)
{
	struct damon_region *ret;

	ret = kmalloc(sizeof(struct damon_region), GFP_KERNEL);
	if (!ret)
		return NULL;
	ret->vm_start = vm_start;
	ret->vm_end = vm_end;
	ret->nr_accesses = 0;
	ret->sampling_addr = damon_rand(ctx, vm_start, vm_end);
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

static void damon_add_task_tail(struct damon_ctx *ctx, struct damon_task *t)
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

/*
 * Returns number of monitoring target tasks
 */
static unsigned int nr_damon_tasks(struct damon_ctx *ctx)
{
	struct damon_task *t;
	unsigned int ret = 0;

	damon_for_each_task(ctx, t)
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

/*
 * Get the mm_struct of the given task
 *
 * Callser should put the mm_struct after use, unless it is NULL.
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
	struct damon_region *piece = NULL, *next;
	unsigned long start;

	if (!r || !nr_pieces)
		return -EINVAL;

	orig_end = r->vm_end;
	sz_orig = r->vm_end - r->vm_start;
	sz_piece = sz_orig / nr_pieces;

	if (!sz_piece)
		return -EINVAL;

	r->vm_end = r->vm_start + sz_piece;
	next = damon_next_region(r);
	for (start = r->vm_end; start + sz_piece <= orig_end;
			start += sz_piece) {
		piece = damon_new_region(ctx, start, start + sz_piece);
		damon_add_region(piece, r, next);
		r = piece;
	}
	if (piece)
		piece->vm_end = orig_end;
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
 * Find the three regions in an address space
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
	struct region gap = {0,}, first_gap = {0,}, second_gap = {0,};
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
	regions[0].start = start;
	regions[0].end = first_gap.start;
	regions[1].start = first_gap.end;
	regions[1].end = second_gap.start;
	regions[2].start = second_gap.end;
	regions[2].end = last_vma->vm_end;

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
	int ret;

	mm = damon_get_mm(t);
	if (!mm)
		return -EINVAL;

	down_read(&mm->mmap_sem);
	ret = damon_three_regions_in_vmas(mm->mmap, regions);
	up_read(&mm->mmap_sem);

	mmput(mm);
	return ret;
}

/*
 * Initialize the monitoring target regions for the given task
 *
 * t	the given target task
 *
 * Because only a number of small portions of the entire address space
 * is acutally mapped to the memory and accessed, monitoring the unmapped
 * regions is wasteful.  That said, because we can deal with small noises,
 * tracking every mapping is not strictly required but could even incur a high
 * overhead if the mapping frequently changes or the number of mappings is
 * high.  The adaptive regions adjustment mechanism will further help to deal
 * with the noises by simply identifying the unmapped areas as a region that
 * has no access.  Moreover, applying the real mappings that would have many
 * unmapped areas inside will make the adaptive mechanism quite complex.  That
 * said, too huge unmapped areas inside the monitoring target should be removed
 * to not take the time for the adaptive mechanism.
 *
 * For the reason, we convert the complex mappings to three distinct regions
 * that cover every mapped areas of the address space.  Also the two gaps
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
	struct damon_region *r;
	struct region regions[3];
	int i;

	if (damon_three_regions_of(t, regions)) {
		pr_err("Failed to get three regions of task %lu\n", t->pid);
		return;
	}

	/* Set the initial three regions of the task */
	for (i = 0; i < 3; i++) {
		r = damon_new_region(c, regions[i].start, regions[i].end);
		damon_add_region_tail(r, t);
	}

	/* Split the middle region into 'min_nr_regions - 2' regions */
	r = damon_nth_region_of(t, 1);
	if (damon_split_region_evenly(c, r, c->min_nr_regions - 2))
		pr_warn("Init middle region failed to be split\n");
}

/* Initialize '->regions_list' of every task */
static void kdamond_init_regions(struct damon_ctx *ctx)
{
	struct damon_task *t;

	damon_for_each_task(ctx, t)
		damon_init_regions_of(ctx, t);
}

/*
 * Check whether the given region has accessed since the last check
 *
 * mm	'mm_struct' for the given virtual address space
 * r	the region to be checked
 */
static void kdamond_check_access(struct damon_ctx *ctx,
			struct mm_struct *mm, struct damon_region *r)
{
	pte_t *pte = NULL;
	pmd_t *pmd = NULL;
	spinlock_t *ptl;

	if (follow_pte_pmd(mm, r->sampling_addr, NULL, &pte, &pmd, &ptl))
		goto mkold;

	/* Read the page table access bit of the page */
	if (pte && pte_young(*pte))
		r->nr_accesses++;
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	else if (pmd && pmd_young(*pmd))
		r->nr_accesses++;
#endif	/* CONFIG_TRANSPARENT_HUGEPAGE */

	spin_unlock(ptl);

mkold:
	/* mkold next target */
	r->sampling_addr = damon_rand(ctx, r->vm_start, r->vm_end);

	if (follow_pte_pmd(mm, r->sampling_addr, NULL, &pte, &pmd, &ptl))
		return;

	if (pte) {
		if (pte_young(*pte)) {
			clear_page_idle(pte_page(*pte));
			set_page_young(pte_page(*pte));
		}
		*pte = pte_mkold(*pte);
	}
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	else if (pmd) {
		if (pmd_young(*pmd)) {
			clear_page_idle(pmd_page(*pmd));
			set_page_young(pte_page(*pte));
		}
		*pmd = pmd_mkold(*pmd);
	}
#endif

	spin_unlock(ptl);
}

/*
 * Check whether a time interval is elapsed
 *
 * baseline	the time to check whether the interval has elapsed since
 * interval	the time interval (microseconds)
 *
 * See whether the given time interval has passed since the given baseline
 * time.  If so, it also updates the baseline to current time for next check.
 *
 * Returns true if the time interval has passed, or false otherwise.
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
	loff_t pos;
	struct file *rfile;

	while (ctx->rbuf_offset) {
		pos = 0;
		rfile = filp_open(ctx->rfile_path, O_CREAT | O_RDWR | O_APPEND,
				0644);
		if (IS_ERR(rfile)) {
			pr_err("Cannot open the result file %s\n",
					ctx->rfile_path);
			return;
		}

		sz = kernel_write(rfile, ctx->rbuf, ctx->rbuf_offset, &pos);
		filp_close(rfile, NULL);

		ctx->rbuf_offset -= sz;
	}
}

/*
 * Write a data into the result buffer
 */
static void damon_write_rbuf(struct damon_ctx *ctx, void *data, ssize_t size)
{
	trace_damon_write_rbuf(data, size);
	if (!ctx->rbuf_len || !ctx->rbuf)
		return;
	if (ctx->rbuf_offset + size > ctx->rbuf_len)
		damon_flush_rbuffer(ctx);

	memcpy(&ctx->rbuf[ctx->rbuf_offset], data, size);
	ctx->rbuf_offset += size;
}

/*
 * Flush the aggregated monitoring results to the result buffer
 *
 * Stores current tracking results to the result buffer and reset 'nr_accesses'
 * of each regions.  The format for the result buffer is as below:
 *
 *   <time> <number of tasks> <array of task infos>
 *
 *   task info: <pid> <number of regions> <array of region infos>
 *   region info: <start address> <end address> <nr_accesses>
 */
static void kdamond_flush_aggregated(struct damon_ctx *c)
{
	struct damon_task *t;
	struct timespec64 now;
	unsigned int nr;

	ktime_get_coarse_ts64(&now);

	damon_write_rbuf(c, &now, sizeof(struct timespec64));
	nr = nr_damon_tasks(c);
	damon_write_rbuf(c, &nr, sizeof(nr));

	damon_for_each_task(c, t) {
		struct damon_region *r;

		damon_write_rbuf(c, &t->pid, sizeof(t->pid));
		nr = nr_damon_regions(t);
		damon_write_rbuf(c, &nr, sizeof(nr));
		damon_for_each_region(r, t) {
			damon_write_rbuf(c, &r->vm_start, sizeof(r->vm_start));
			damon_write_rbuf(c, &r->vm_end, sizeof(r->vm_end));
			damon_write_rbuf(c, &r->nr_accesses,
					sizeof(r->nr_accesses));
			r->nr_accesses = 0;
		}
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
 * t		task that merge operation will make change
 * thres	merge regions having '->nr_accesses' diff smaller than this
 */
static void damon_merge_regions_of(struct damon_task *t, unsigned int thres)
{
	struct damon_region *r, *prev = NULL, *next;

	damon_for_each_region_safe(r, next, t) {
		if (!prev || prev->vm_end != r->vm_start)
			goto next;
		if (diff_of(prev->nr_accesses, r->nr_accesses) > thres)
			goto next;
		damon_merge_two_regions(prev, r);
		continue;
next:
		prev = r;
	}
}

/*
 * Merge adjacent regions having similar access frequencies
 *
 * threshold	merge regions havind nr_accesses diff larger than this
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
 * Split a region into two small regions
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

	damon_add_region(new, r, damon_next_region(r));
}

static void damon_split_regions_of(struct damon_ctx *ctx, struct damon_task *t)
{
	struct damon_region *r, *next;
	unsigned long sz_left_region;

	damon_for_each_region_safe(r, next, t) {
		/*
		 * Randomly select size of left sub-region to be at least
		 * 10 percent and at most 90% of original region
		 */
		sz_left_region = (prandom_u32_state(&ctx->rndseed) % 9 + 1) *
			(r->vm_end - r->vm_start) / 10;
		/* Do not allow blank region */
		if (sz_left_region == 0)
			continue;
		damon_split_region_at(ctx, r, sz_left_region);
	}
}

/*
 * splits every target regions into two randomly-sized regions
 *
 * This function splits every target regions into two random-sized regions if
 * current total number of the regions is smaller than the half of the
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
 * Check whether it is time to check and apply the dynamic mmap changes
 *
 * Returns true if it is.
 */
static bool kdamond_need_update_regions(struct damon_ctx *ctx)
{
	return damon_check_reset_time_interval(&ctx->last_regions_update,
			ctx->regions_update_interval);
}

static bool damon_intersect(struct damon_region *r, struct region *re)
{
	return !(r->vm_end <= re->start || re->end <= r->vm_start);
}

/*
 * Update damon regions for the three big regions of the given task
 *
 * t		the given task
 * bregions	the three big regions of the task
 */
static void damon_apply_three_regions(struct damon_ctx *ctx,
		struct damon_task *t, struct region bregions[3])
{
	struct damon_region *r, *next;
	unsigned int i = 0;

	/* Remove regions which isn't in the three big regions now */
	damon_for_each_region_safe(r, next, t) {
		for (i = 0; i < 3; i++) {
			if (damon_intersect(r, &bregions[i]))
				break;
		}
		if (i == 3)
			damon_destroy_region(r);
	}

	/* Adjust intersecting regions to fit with the threee big regions */
	for (i = 0; i < 3; i++) {
		struct damon_region *first = NULL, *last;
		struct damon_region *newr;
		struct region *br;

		br = &bregions[i];
		/* Get the first and last regions which intersects with br */
		damon_for_each_region(r, t) {
			if (damon_intersect(r, br)) {
				if (!first)
					first = r;
				last = r;
			}
			if (r->vm_start >= br->end)
				break;
		}
		if (!first) {
			/* no damon_region intersects with this big region */
			newr = damon_new_region(ctx, br->start, br->end);
			damon_add_region(newr, damon_prev_region(r), r);
		} else {
			first->vm_start = br->start;
			last->vm_end = br->end;
		}
	}
}

/*
 * Update regions for current memory mappings
 */
static void kdamond_update_regions(struct damon_ctx *ctx)
{
	struct region three_regions[3];
	struct damon_task *t;

	damon_for_each_task(ctx, t) {
		if (damon_three_regions_of(t, three_regions))
			continue;
		damon_apply_three_regions(ctx, t, three_regions);
	}
}

/*
 * Check whether current monitoring should be stopped
 *
 * If users asked to stop, need stop.  Even though no user has asked to stop,
 * need stop if every target task has dead.
 *
 * Returns true if need to stop current monitoring.
 */
static bool kdamond_need_stop(struct damon_ctx *ctx)
{
	struct damon_task *t;
	struct task_struct *task;
	bool stop;

	spin_lock(&ctx->kdamond_lock);
	stop = ctx->kdamond_stop;
	spin_unlock(&ctx->kdamond_lock);
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
	struct mm_struct *mm;
	unsigned long max_nr_accesses;

	pr_info("kdamond (%d) starts\n", ctx->kdamond->pid);
	kdamond_init_regions(ctx);
	while (!kdamond_need_stop(ctx)) {
		max_nr_accesses = 0;
		damon_for_each_task(ctx, t) {
			mm = damon_get_mm(t);
			if (!mm)
				continue;
			damon_for_each_region(r, t) {
				kdamond_check_access(ctx, mm, r);
				if (r->nr_accesses > max_nr_accesses)
					max_nr_accesses = r->nr_accesses;
			}
			mmput(mm);
		}
		if (ctx->sample_cb)
			ctx->sample_cb(ctx);

		if (kdamond_aggregate_interval_passed(ctx)) {
			kdamond_merge_regions(ctx, max_nr_accesses / 10);
			if (ctx->aggregate_cb)
				ctx->aggregate_cb(ctx);
			kdamond_flush_aggregated(ctx);
			kdamond_split_regions(ctx);
		}

		if (kdamond_need_update_regions(ctx))
			kdamond_update_regions(ctx);

		usleep_range(ctx->sample_interval, ctx->sample_interval + 1);
	}
	damon_flush_rbuffer(ctx);
	damon_for_each_task(ctx, t) {
		damon_for_each_region_safe(r, next, t)
			damon_destroy_region(r);
	}
	pr_info("kdamond (%d) finishes\n", ctx->kdamond->pid);
	spin_lock(&ctx->kdamond_lock);
	ctx->kdamond = NULL;
	spin_unlock(&ctx->kdamond_lock);
	return 0;
}

/*
 * Controller functions
 */

/*
 * Start or stop the kdamond
 *
 * Returns 0 if success, negative error code otherwise.
 */
static int damon_turn_kdamond(struct damon_ctx *ctx, bool on)
{
	spin_lock(&ctx->kdamond_lock);
	ctx->kdamond_stop = !on;
	if (!ctx->kdamond && on) {
		ctx->kdamond = kthread_run(kdamond_fn, ctx, "kdamond");
		if (!ctx->kdamond)
			goto fail;
		goto success;
	}
	if (ctx->kdamond && !on) {
		spin_unlock(&ctx->kdamond_lock);
		while (true) {
			spin_lock(&ctx->kdamond_lock);
			if (!ctx->kdamond)
				goto success;
			spin_unlock(&ctx->kdamond_lock);

			usleep_range(ctx->sample_interval,
					ctx->sample_interval * 2);
		}
	}

	/* tried to turn on while turned on, or turn off while turned off */

fail:
	spin_unlock(&ctx->kdamond_lock);
	return -EINVAL;

success:
	spin_unlock(&ctx->kdamond_lock);
	return 0;
}

int damon_start(struct damon_ctx *ctx)
{
	return damon_turn_kdamond(ctx, true);
}

int damon_stop(struct damon_ctx *ctx)
{
	return damon_turn_kdamond(ctx, false);
}

/*
 * This function should not be called while the kdamond is running.
 */
int damon_set_pids(struct damon_ctx *ctx,
			unsigned long *pids, ssize_t nr_pids)
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
		damon_add_task_tail(ctx, t);
	}

	return 0;
}

/*
 * Set attributes for the recording
 *
 * ctx		target kdamond context
 * rbuf_len	length of the result buffer
 * rfile_path	path to the monitor result files
 *
 * Setting 'rbuf_len' 0 disables recording.
 *
 * This function should not be called while the kdamond is running.
 *
 * Returns 0 on success, negative error code otherwise.
 */
int damon_set_recording(struct damon_ctx *ctx,
				unsigned int rbuf_len, char *rfile_path)
{
	size_t rfile_path_len;

	if (rbuf_len > 4 * 1024 * 1024) {
		pr_err("too long (>%d) result buffer length\n",
				4 * 1024 * 1024);
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
	kfree(ctx->rfile_path);
	if (!rbuf_len)
		return 0;

	ctx->rbuf = kvmalloc(rbuf_len, GFP_KERNEL);
	if (!ctx->rbuf)
		return -ENOMEM;
	ctx->rfile_path = kmalloc(rfile_path_len + 1, GFP_KERNEL);
	if (!ctx->rfile_path)
		return -ENOMEM;
	strncpy(ctx->rfile_path, rfile_path, rfile_path_len + 1);
	return 0;
}

/*
 * Set attributes for the monitoring
 *
 * sample_int		time interval between samplings
 * aggr_int		time interval between aggregations
 * regions_update_int	time interval between vma update checks
 * min_nr_reg		minimal number of regions
 * max_nr_reg		maximum number of regions
 *
 * This function should not be called while the kdamond is running.
 * Every time interval is in micro-seconds.
 *
 * Returns 0 on success, negative error code otherwise.
 */
int damon_set_attrs(struct damon_ctx *ctx, unsigned long sample_int,
		unsigned long aggr_int, unsigned long regions_update_int,
		unsigned long min_nr_reg, unsigned long max_nr_reg)
{
	if (min_nr_reg < 3) {
		pr_err("min_nr_regions (%lu) should be bigger than 2\n",
				min_nr_reg);
		return -EINVAL;
	}
	if (min_nr_reg >= ctx->max_nr_regions) {
		pr_err("invalid nr_regions.  min (%lu) >= max (%lu)\n",
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
 * debugfs functions
 */

static ssize_t debugfs_monitor_on_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = &damon_user_ctx;
	char monitor_on_buf[5];
	bool monitor_on;
	int ret;

	spin_lock(&ctx->kdamond_lock);
	monitor_on = ctx->kdamond != NULL;
	spin_unlock(&ctx->kdamond_lock);

	ret = snprintf(monitor_on_buf, 5, monitor_on ? "on\n" : "off\n");

	return simple_read_from_buffer(buf, count, ppos, monitor_on_buf, ret);
}

static ssize_t debugfs_monitor_on_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = &damon_user_ctx;
	ssize_t ret;
	bool on = false;
	char cmdbuf[5];

	ret = simple_write_to_buffer(cmdbuf, 5, ppos, buf, count);
	if (ret < 0)
		return ret;

	if (sscanf(cmdbuf, "%s", cmdbuf) != 1)
		return -EINVAL;
	if (!strncmp(cmdbuf, "on", 5))
		on = true;
	else if (!strncmp(cmdbuf, "off", 5))
		on = false;
	else
		return -EINVAL;

	if (damon_turn_kdamond(ctx, on))
		return -EINVAL;

	return ret;
}

static ssize_t damon_sprint_pids(struct damon_ctx *ctx, char *buf, ssize_t len)
{
	struct damon_task *t;
	int written = 0;
	int rc;

	damon_for_each_task(ctx, t) {
		rc = snprintf(&buf[written], len - written, "%lu ", t->pid);
		if (!rc)
			return -ENOMEM;
		written += rc;
	}
	if (written)
		written -= 1;
	written += snprintf(&buf[written], len - written, "\n");
	return written;
}

static ssize_t debugfs_pids_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = &damon_user_ctx;
	ssize_t len;
	char pids_buf[320];

	len = damon_sprint_pids(ctx, pids_buf, 320);
	if (len < 0)
		return len;

	return simple_read_from_buffer(buf, count, ppos, pids_buf, len);
}

/*
 * Converts a string into an array of unsigned long integers
 *
 * Returns an array of unsigned long integers if the conversion success, or
 * NULL otherwise.
 */
static unsigned long *str_to_pids(const char *str, ssize_t len,
				ssize_t *nr_pids)
{
	unsigned long *pids;
	const int max_nr_pids = 32;
	unsigned long pid;
	int pos = 0, parsed, ret;

	*nr_pids = 0;
	pids = kmalloc_array(max_nr_pids, sizeof(unsigned long), GFP_KERNEL);
	if (!pids)
		return NULL;
	while (*nr_pids < max_nr_pids && pos < len) {
		ret = sscanf(&str[pos], "%lu%n", &pid, &parsed);
		pos += parsed;
		if (ret != 1)
			break;
		pids[*nr_pids] = pid;
		*nr_pids += 1;
	}
	if (*nr_pids == 0) {
		kfree(pids);
		pids = NULL;
	}

	return pids;
}

static ssize_t debugfs_pids_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = &damon_user_ctx;
	char *kbuf;
	unsigned long *targets;
	ssize_t nr_targets;
	ssize_t ret;

	kbuf = kmalloc_array(count, sizeof(char), GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	ret = simple_write_to_buffer(kbuf, 512, ppos, buf, count);
	if (ret < 0)
		goto out;

	targets = str_to_pids(kbuf, ret, &nr_targets);
	if (!targets) {
		ret = -ENOMEM;
		goto out;
	}

	spin_lock(&ctx->kdamond_lock);
	if (ctx->kdamond)
		goto monitor_running;

	damon_set_pids(ctx, targets, nr_targets);
	spin_unlock(&ctx->kdamond_lock);

	goto free_targets_out;

monitor_running:
	spin_unlock(&ctx->kdamond_lock);
	pr_err("%s: kdamond is running. Turn it off first.\n", __func__);
	ret = -EINVAL;
free_targets_out:
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

	ret = snprintf(record_buf, ARRAY_SIZE(record_buf), "%u %s\n",
			ctx->rbuf_len, ctx->rfile_path);
	return simple_read_from_buffer(buf, count, ppos, record_buf, ret);
}

static ssize_t debugfs_record_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = &damon_user_ctx;
	char *kbuf;
	unsigned int rbuf_len;
	char rfile_path[MAX_RFILE_PATH_LEN];
	ssize_t ret;

	kbuf = kmalloc_array(count, sizeof(char), GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	ret = simple_write_to_buffer(kbuf, count, ppos, buf, count);
	if (ret < 0)
		goto out;
	if (sscanf(kbuf, "%u %s",
				&rbuf_len, rfile_path) != 2) {
		ret = -EINVAL;
		goto out;
	}

	spin_lock(&ctx->kdamond_lock);
	if (ctx->kdamond)
		goto monitor_running;

	damon_set_recording(ctx, rbuf_len, rfile_path);
	spin_unlock(&ctx->kdamond_lock);

	goto out;

monitor_running:
	spin_unlock(&ctx->kdamond_lock);
	pr_err("%s: kdamond is running. Turn it off first.\n", __func__);
	ret = -EINVAL;
out:
	kfree(kbuf);
	return ret;
}


static ssize_t debugfs_attrs_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = &damon_user_ctx;
	char kbuf[128];
	int ret;

	ret = snprintf(kbuf, ARRAY_SIZE(kbuf), "%lu %lu %lu %lu %lu\n",
			ctx->sample_interval, ctx->aggr_interval,
			ctx->regions_update_interval, ctx->min_nr_regions,
			ctx->max_nr_regions);

	return simple_read_from_buffer(buf, count, ppos, kbuf, ret);
}

static ssize_t debugfs_attrs_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = &damon_user_ctx;
	unsigned long s, a, r, minr, maxr;
	char *kbuf;
	ssize_t ret;

	kbuf = kmalloc_array(count, sizeof(char), GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	ret = simple_write_to_buffer(kbuf, count, ppos, buf, count);
	if (ret < 0)
		goto out;

	if (sscanf(kbuf, "%lu %lu %lu %lu %lu",
				&s, &a, &r, &minr, &maxr) != 5) {
		ret = -EINVAL;
		goto out;
	}

	spin_lock(&ctx->kdamond_lock);
	if (ctx->kdamond)
		goto monitor_running;

	damon_set_attrs(ctx, s, a, r, minr, maxr);
	spin_unlock(&ctx->kdamond_lock);

	goto out;

monitor_running:
	spin_unlock(&ctx->kdamond_lock);
	pr_err("%s: kdamond is running. Turn it off first.\n", __func__);
	ret = -EINVAL;
out:
	kfree(kbuf);
	return ret;
}

static const struct file_operations monitor_on_fops = {
	.owner = THIS_MODULE,
	.read = debugfs_monitor_on_read,
	.write = debugfs_monitor_on_write,
};

static const struct file_operations pids_fops = {
	.owner = THIS_MODULE,
	.read = debugfs_pids_read,
	.write = debugfs_pids_write,
};

static const struct file_operations record_fops = {
	.owner = THIS_MODULE,
	.read = debugfs_record_read,
	.write = debugfs_record_write,
};

static const struct file_operations attrs_fops = {
	.owner = THIS_MODULE,
	.read = debugfs_attrs_read,
	.write = debugfs_attrs_write,
};

static struct dentry *debugfs_root;

static int __init debugfs_init(void)
{
	const char * const file_names[] = {"attrs", "record",
		"pids", "monitor_on"};
	const struct file_operations *fops[] = {&attrs_fops, &record_fops,
		&pids_fops, &monitor_on_fops};
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

	ctx->rbuf_offset = 0;
	rc = damon_set_recording(ctx, 1024 * 1024, "/damon.data");
	if (rc)
		return rc;

	ctx->kdamond = NULL;
	ctx->kdamond_stop = false;
	spin_lock_init(&ctx->kdamond_lock);

	prandom_seed_state(&ctx->rndseed, 42);
	INIT_LIST_HEAD(&ctx->tasks_list);

	ctx->sample_cb = NULL;
	ctx->aggregate_cb = NULL;

	return 0;
}

static int __init damon_init(void)
{
	int rc;

	pr_info("init\n");

	rc = damon_init_user_ctx();
	if (rc)
		return rc;

	return debugfs_init();
}

static void __exit damon_exit(void)
{
	damon_turn_kdamond(&damon_user_ctx, false);
	debugfs_remove_recursive(debugfs_root);

	kfree(damon_user_ctx.rbuf);
	kfree(damon_user_ctx.rfile_path);

	pr_info("exit\n");
}

module_init(damon_init);
module_exit(damon_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SeongJae Park <sjpark@amazon.de>");
MODULE_DESCRIPTION("DAMON: Data Access MONitor");

#include "damon-test.h"
