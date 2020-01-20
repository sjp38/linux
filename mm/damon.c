// SPDX-License-Identifier: GPL-2.0
/*
 * Data Access Monitor
 *
 * Copyright 2019 Amazon.com, Inc. or its affiliates.  All rights reserved.
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 */

#define pr_fmt(fmt) "damon: " fmt

#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/page_idle.h>
#include <linux/random.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
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

/*
 * For each 'sample_interval', DAMON checks whether each region is accessed or
 * not.  It aggregates and keeps the access information (number of accesses to
 * each region) for 'aggr_interval' and then flushes it to the result buffer if
 * an 'aggr_interval' surpassed.
 *
 * All time intervals are in micro-seconds.
 */
static unsigned long sample_interval = 5 * 1000;
static unsigned long aggr_interval = 100 * 1000;

static struct timespec64 last_aggregate_time;

static unsigned long min_nr_regions = 10;
static unsigned long max_nr_regions = 1000;

/* result buffer */
#define DAMON_LEN_RBUF	(1024 * 1024 * 4)
static char damon_rbuf[DAMON_LEN_RBUF];
static unsigned int damon_rbuf_offset;

/* result file */
#define LEN_RES_FILE_PATH	256
static char rfile_path[LEN_RES_FILE_PATH] = "/damon.data";

static struct task_struct *kdamond;
static bool kdamond_stop;

/* Protects read/write of kdamond and kdamond_stop */
static DEFINE_SPINLOCK(kdamond_lock);

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
static int damon_split_region_evenly(struct damon_region *r,
				unsigned int nr_pieces)
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
		piece = damon_new_region(start, start + sz_piece);
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
 * separated by the two biggest unmapped regions in the space.
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
static void damon_init_regions_of(struct damon_task *t)
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
		r = damon_new_region(regions[i].start, regions[i].end);
		damon_add_region_tail(r, t);
	}

	/* Split the middle region into 'min_nr_regions - 2' regions */
	r = damon_nth_region_of(t, 1);
	if (damon_split_region_evenly(r, min_nr_regions - 2))
		pr_warn("Init middle region failed to be split\n");
}

/* Initialize '->regions_list' of every task */
static void kdamond_init_regions(void)
{
	struct damon_task *t;

	damon_for_each_task(t)
		damon_init_regions_of(t);
}

/*
 * Check whether the given region has accessed since the last check
 *
 * mm	'mm_struct' for the given virtual address space
 * r	the region to be checked
 */
static void kdamond_check_access(struct mm_struct *mm, struct damon_region *r)
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
	r->sampling_addr = damon_rand(r->vm_start, r->vm_end);

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
	if ((timespec64_to_ns(&now) - timespec64_to_ns(baseline)) / 1000 <
			interval)
		return false;
	*baseline = now;
	return true;
}

/*
 * Check whether it is time to flush the aggregated information
 */
static bool kdamond_aggregate_interval_passed(void)
{
	return damon_check_reset_time_interval(&last_aggregate_time,
			aggr_interval);
}

/*
 * Flush the content in the result buffer to the result file
 */
static void damon_flush_rbuffer(void)
{
	ssize_t sz;
	loff_t pos;
	struct file *rfile;

	while (damon_rbuf_offset) {
		pos = 0;
		rfile = filp_open(rfile_path, O_CREAT | O_RDWR | O_APPEND,
				0644);
		if (IS_ERR(rfile)) {
			pr_err("Cannot open the result file %s\n", rfile_path);
			return;
		}

		sz = kernel_write(rfile, damon_rbuf, damon_rbuf_offset, &pos);
		filp_close(rfile, NULL);

		damon_rbuf_offset -= sz;
	}
}

/*
 * Write a data into the result buffer
 */
static void damon_write_rbuf(void *data, ssize_t size)
{
	if (damon_rbuf_offset + size > DAMON_LEN_RBUF)
		damon_flush_rbuffer();

	memcpy(&damon_rbuf[damon_rbuf_offset], data, size);
	damon_rbuf_offset += size;
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
static void kdamond_flush_aggregated(void)
{
	struct damon_task *t;
	struct timespec64 now;
	unsigned int nr;

	ktime_get_coarse_ts64(&now);

	damon_write_rbuf(&now, sizeof(struct timespec64));
	nr = nr_damon_tasks();
	damon_write_rbuf(&nr, sizeof(nr));

	damon_for_each_task(t) {
		struct damon_region *r;

		damon_write_rbuf(&t->pid, sizeof(t->pid));
		nr = nr_damon_regions(t);
		damon_write_rbuf(&nr, sizeof(nr));
		damon_for_each_region(r, t) {
			damon_write_rbuf(&r->vm_start, sizeof(r->vm_start));
			damon_write_rbuf(&r->vm_end, sizeof(r->vm_end));
			damon_write_rbuf(&r->nr_accesses,
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
static void kdamond_merge_regions(unsigned int threshold)
{
	struct damon_task *t;

	damon_for_each_task(t)
		damon_merge_regions_of(t, threshold);
}

/*
 * Split a region into two small regions
 *
 * r		the region to be split
 * sz_r		size of the first sub-region that will be made
 */
static void damon_split_region_at(struct damon_region *r, unsigned long sz_r)
{
	struct damon_region *new;

	new = damon_new_region(r->vm_start + sz_r, r->vm_end);
	r->vm_end = new->vm_start;

	damon_add_region(new, r, damon_next_region(r));
}

static void damon_split_regions_of(struct damon_task *t)
{
	struct damon_region *r, *next;
	unsigned long sz_left_region;

	damon_for_each_region_safe(r, next, t) {
		/*
		 * Randomly select size of left sub-region to be at least
		 * 10 percent and at most 90% of original region
		 */
		sz_left_region = (prandom_u32_state(&rndseed) % 9 + 1) *
			(r->vm_end - r->vm_start) / 10;
		/* Do not allow blank region */
		if (sz_left_region == 0)
			continue;
		damon_split_region_at(r, sz_left_region);
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
static void kdamond_split_regions(void)
{
	struct damon_task *t;
	unsigned int nr_regions = 0;

	damon_for_each_task(t)
		nr_regions += nr_damon_regions(t);
	if (nr_regions > max_nr_regions / 2)
		return;

	damon_for_each_task(t)
		damon_split_regions_of(t);
}

/*
 * Check whether current monitoring should be stopped
 *
 * If users asked to stop, need stop.  Even though no user has asked to stop,
 * need stop if every target task has dead.
 *
 * Returns true if need to stop current monitoring.
 */
static bool kdamond_need_stop(void)
{
	struct damon_task *t;
	struct task_struct *task;
	bool stop;

	spin_lock(&kdamond_lock);
	stop = kdamond_stop;
	spin_unlock(&kdamond_lock);
	if (stop)
		return true;

	damon_for_each_task(t) {
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
	struct damon_task *t;
	struct damon_region *r, *next;
	struct mm_struct *mm;
	unsigned long max_nr_accesses;

	pr_info("kdamond (%d) starts\n", kdamond->pid);
	kdamond_init_regions();
	while (!kdamond_need_stop()) {
		max_nr_accesses = 0;
		damon_for_each_task(t) {
			mm = damon_get_mm(t);
			if (!mm)
				continue;
			damon_for_each_region(r, t) {
				kdamond_check_access(mm, r);
				if (r->nr_accesses > max_nr_accesses)
					max_nr_accesses = r->nr_accesses;
			}
			mmput(mm);
		}

		if (kdamond_aggregate_interval_passed()) {
			kdamond_merge_regions(max_nr_accesses / 10);
			kdamond_flush_aggregated();
			kdamond_split_regions();
		}

		usleep_range(sample_interval, sample_interval + 1);
	}
	damon_flush_rbuffer();
	damon_for_each_task(t) {
		damon_for_each_region_safe(r, next, t)
			damon_destroy_region(r);
	}
	pr_info("kdamond (%d) finishes\n", kdamond->pid);
	spin_lock(&kdamond_lock);
	kdamond = NULL;
	spin_unlock(&kdamond_lock);
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
static int damon_turn_kdamond(bool on)
{
	spin_lock(&kdamond_lock);
	kdamond_stop = !on;
	if (!kdamond && on) {
		kdamond = kthread_run(kdamond_fn, NULL, "kdamond");
		if (!kdamond)
			goto fail;
		goto success;
	}
	if (kdamond && !on) {
		spin_unlock(&kdamond_lock);
		while (true) {
			spin_lock(&kdamond_lock);
			if (!kdamond)
				goto success;
			spin_unlock(&kdamond_lock);

			usleep_range(sample_interval, sample_interval * 2);
		}
	}

	/* tried to turn on while turned on, or turn off while turned off */

fail:
	spin_unlock(&kdamond_lock);
	return -EINVAL;

success:
	spin_unlock(&kdamond_lock);
	return 0;
}

static inline bool damon_is_target_pid(unsigned long pid)
{
	struct damon_task *t;

	damon_for_each_task(t) {
		if (t->pid == pid)
			return true;
	}
	return false;
}

/*
 * This function should not be called while the kdamond is running.
 */
static long damon_set_pids(unsigned long *pids, ssize_t nr_pids)
{
	ssize_t i;
	struct damon_task *t, *next;

	/* Remove unselected tasks */
	damon_for_each_task_safe(t, next) {
		for (i = 0; i < nr_pids; i++) {
			if (pids[i] == t->pid)
				break;
		}
		if (i != nr_pids)
			continue;
		damon_destroy_task(t);
	}

	/* Add new tasks */
	for (i = 0; i < nr_pids; i++) {
		if (damon_is_target_pid(pids[i]))
			continue;
		t = damon_new_task(pids[i]);
		if (!t) {
			pr_err("Failed to alloc damon_task\n");
			return -ENOMEM;
		}
		damon_add_task_tail(t);
	}

	return 0;
}

/*
 * Set attributes for the monitoring
 *
 * sample_int		time interval between samplings
 * aggr_int		time interval between aggregations
 * min_nr_reg		minimal number of regions
 * max_nr_reg		maximum number of regions
 * path_to_rfile	path to the monitor result files
 *
 * This function should not be called while the kdamond is running.
 * Every time interval is in micro-seconds.
 *
 * Returns 0 on success, negative error code otherwise.
 */
static long damon_set_attrs(unsigned long sample_int,
		unsigned long aggr_int,
		unsigned long min_nr_reg, unsigned long max_nr_reg,
		char *path_to_rfile)
{
	if (strnlen(path_to_rfile, LEN_RES_FILE_PATH) >= LEN_RES_FILE_PATH) {
		pr_err("too long (>%d) result file path %s\n",
				LEN_RES_FILE_PATH, path_to_rfile);
		return -EINVAL;
	}
	if (min_nr_reg < 3) {
		pr_err("min_nr_regions (%lu) should be bigger than 2\n",
				min_nr_reg);
		return -EINVAL;
	}
	if (min_nr_reg >= max_nr_regions) {
		pr_err("invalid nr_regions.  min (%lu) >= max (%lu)\n",
				min_nr_reg, max_nr_reg);
		return -EINVAL;
	}

	sample_interval = sample_int;
	aggr_interval = aggr_int;
	min_nr_regions = min_nr_reg;
	max_nr_regions = max_nr_reg;
	strncpy(rfile_path, path_to_rfile, LEN_RES_FILE_PATH);
	return 0;
}

static int __init damon_init(void)
{
	pr_info("init\n");

	prandom_seed_state(&rndseed, 42);
	ktime_get_coarse_ts64(&last_aggregate_time);
	return 0;
}

static void __exit damon_exit(void)
{
	damon_turn_kdamond(false);
	pr_info("exit\n");
}

module_init(damon_init);
module_exit(damon_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SeongJae Park <sjpark@amazon.de>");
MODULE_DESCRIPTION("DAMON: Data Access MONitor");
