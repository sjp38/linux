/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DAMON api
 *
 * Copyright 2019-2020 Amazon.com, Inc. or its affiliates.
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 */

#ifndef _DAMON_H_
#define _DAMON_H_

#include <linux/random.h>
#include <linux/mutex.h>
#include <linux/time64.h>
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
 * @sampling_addr:	Address of the sample for the next access check.
 * @nr_accesses:	Access frequency of this region.
 * @list:		List head for siblings.
 * @age:		Age of this region.
 * @last_nr_accesses:	Internal value for age calculation.
 *
 * @age is initially zero, increased for each aggregation interval, and reset
 * to zero again if the access frequency is significantly changed.  If two
 * regions are merged into a new region, both @nr_accesses and @age of the new
 * region are set as region size-weighted average of those of the two regions.
 */
struct damon_region {
	struct damon_addr_range ar;
	unsigned long sampling_addr;
	unsigned int nr_accesses;
	struct list_head list;

	unsigned int age;
	unsigned int last_nr_accesses;
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
 * an adress of an mm_struct.
 */
struct damon_target {
	unsigned long id;
	struct list_head regions_list;
	struct list_head list;
};

/**
 * enum damos_action - Represents an action of a Data Access Monitoring-based
 * Operation Scheme.
 *
 * @DAMOS_WILLNEED:	Call ``madvise()`` for the region with MADV_WILLNEED.
 * @DAMOS_COLD:		Call ``madvise()`` for the region with MADV_COLD.
 * @DAMOS_PAGEOUT:	Call ``madvise()`` for the region with MADV_PAGEOUT.
 * @DAMOS_HUGEPAGE:	Call ``madvise()`` for the region with MADV_HUGEPAGE.
 * @DAMOS_NOHUGEPAGE:	Call ``madvise()`` for the region with MADV_NOHUGEPAGE.
 * @DAMOS_STAT:		Do nothing but count the stat.
 */
enum damos_action {
	DAMOS_WILLNEED,
	DAMOS_COLD,
	DAMOS_PAGEOUT,
	DAMOS_HUGEPAGE,
	DAMOS_NOHUGEPAGE,
	DAMOS_STAT,		/* Do nothing but only record the stat */
};

/**
 * struct damos - Represents a Data Access Monitoring-based Operation Scheme.
 * @min_sz_region:	Minimum size of target regions.
 * @max_sz_region:	Maximum size of target regions.
 * @min_nr_accesses:	Minimum ``->nr_accesses`` of target regions.
 * @max_nr_accesses:	Maximum ``->nr_accesses`` of target regions.
 * @min_age_region:	Minimum age of target regions.
 * @max_age_region:	Maximum age of target regions.
 * @action:		&damo_action to be applied to the target regions.
 * @stat_count:		Total number of regions that this scheme is applied.
 * @stat_sz:		Total size of regions that this scheme is applied.
 * @list:		List head for siblings.
 *
 * For each aggregation interval, DAMON applies @action to monitoring target
 * regions fit in the condition and updates the statistics.  Note that both
 * the minimums and the maximums are inclusive.
 */
struct damos {
	unsigned long min_sz_region;
	unsigned long max_sz_region;
	unsigned int min_nr_accesses;
	unsigned int max_nr_accesses;
	unsigned int min_age_region;
	unsigned int max_age_region;
	enum damos_action action;
	unsigned long stat_count;
	unsigned long stat_sz;
	struct list_head list;
};

/**
 * struct damon_ctx - Represents a context for each monitoring.  This is the
 * main interface that allows users to set the attributes and get the results
 * of the monitoring.
 *
 * @sample_interval:		The time between access samplings.
 * @aggr_interval:		The time between monitor results aggregations.
 * @regions_update_interval:	The time between monitor regions updates.
 * @min_nr_regions:		The minimum number of monitoring regions.
 * @max_nr_regions:		The maximum number of monitoring regions.
 *
 * For each @sample_interval, DAMON checks whether each region is accessed or
 * not.  It aggregates and keeps the access information (number of accesses to
 * each region) for @aggr_interval time.  DAMON also checks whether the target
 * memory regions need update (e.g., by ``mmap()`` calls from the application,
 * in case of virtual memory monitoring) and applies the changes for each
 * @regions_update_interval.  All time intervals are in micro-seconds.
 *
 * @rbuf: In-memory buffer for monitoring result recording.
 * @rbuf_len: The length of @rbuf.
 * @rbuf_offset: The offset for next write to @rbuf.
 * @rfile_path: Record file path.
 *
 * If @rbuf, @rbuf_len, and @rfile_path are set, the monitored results are
 * automatically stored in @rfile_path file.
 *
 * @kdamond:		Kernel thread who does the monitoring.
 * @kdamond_stop:	Notifies whether kdamond should stop.
 * @kdamond_lock:	Mutex for the synchronizations with @kdamond.
 *
 * For each monitoring request (damon_start()), a kernel thread for the
 * monitoring is created.  The pointer to the thread is stored in @kdamond.
 *
 * Once started, the monitoring thread runs until explicitly required to be
 * terminated or every monitoring target is invalid.  The validity of the
 * targets is checked via the @target_valid callback.  The termination can also
 * be explicitly requested by writing non-zero to @kdamond_stop.  The thread
 * sets @kdamond to NULL when it terminates.  Therefore, users can know whether
 * the monitoring is ongoing or terminated by reading @kdamond.  Reads and
 * writes to @kdamond and @kdamond_stop from outside of the monitoring thread
 * must be protected by @kdamond_lock.
 *
 * Note that the monitoring thread protects only @kdamond and @kdamond_stop via
 * @kdamond_lock.  Accesses to other fields must be protected by themselves.
 *
 * @targets_list:	Head of monitoring targets (&damon_target) list.
 * @schemes_list:	Head of schemes (&damos) list.
 *
 * @priv:		Private data for the monitoring requester.
 *
 * @init_target_regions:	Constructs initial monitoring target regions.
 * @update_target_regions:	Updates monitoring target regions.
 * @prepare_access_checks:	Prepares next access check of target regions.
 * @check_accesses:		Checks the access of target regions.
 * @target_valid:		Determine if the target is valid.
 * @sample_cb:			Called for each sampling interval.
 * @aggregate_cb:		Called for each aggregation interval.
 *
 * DAMON can be extended for various address spaces by users.  For this, users
 * can register the target address space dependent low level functions for
 * their usecases via the callback pointers of the context.  The monitoring
 * thread calls @init_target_regions before starting the monitoring,
 * @update_target_regions for each @regions_update_interval, and
 * @prepare_access_checks, @check_accesses, and @target_valid for each
 * @sample_interval.
 *
 * @init_target_regions should construct proper monitoring target regions and
 * link those to the DAMON context struct.
 * @update_target_regions should update the monitoring target regions for
 * current status.
 * @prepare_access_checks should manipulate the monitoring regions to be
 * prepare for the next access check.
 * @check_accesses should check the accesses to each region that made after the
 * last preparation and update the `->nr_accesses` of each region.
 * @target_valid should check whether the target is still valid for the
 * monitoring.
 *
 * @sample_cb and @aggregate_cb are called from @kdamond for each of the
 * sampling intervals and aggregation intervals, respectively.  Therefore,
 * users can safely access to the monitoring results via @targets_list without
 * additional protection of @kdamond_lock.  For the reason, users are
 * recommended to use these callback for the accesses to the results.
 *
 * @stop_cb is called from @kdamond just before its termination.  This would be
 * a good point for the cleanup of @priv.
 */
struct damon_ctx {
	unsigned long sample_interval;
	unsigned long aggr_interval;
	unsigned long regions_update_interval;
	unsigned long min_nr_regions;
	unsigned long max_nr_regions;

	struct timespec64 last_aggregation;
	struct timespec64 last_regions_update;

	unsigned char *rbuf;
	unsigned int rbuf_len;
	unsigned int rbuf_offset;
	char *rfile_path;

	struct task_struct *kdamond;
	bool kdamond_stop;
	struct mutex kdamond_lock;

	struct list_head targets_list;	/* 'damon_target' objects */
	struct list_head schemes_list;	/* 'damos' objects */

	void *priv;

	/* callbacks */
	void (*init_target_regions)(struct damon_ctx *context);
	void (*update_target_regions)(struct damon_ctx *context);
	void (*prepare_access_checks)(struct damon_ctx *context);
	unsigned int (*check_accesses)(struct damon_ctx *context);
	bool (*target_valid)(struct damon_target *target);
	void (*sample_cb)(struct damon_ctx *context);
	void (*aggregate_cb)(struct damon_ctx *context);
	void (*stop_cb)(struct damon_ctx *context);
};

/* Reference callback implementations for virtual memory */
void kdamond_init_vm_regions(struct damon_ctx *ctx);
void kdamond_update_vm_regions(struct damon_ctx *ctx);
void kdamond_prepare_vm_access_checks(struct damon_ctx *ctx);
unsigned int kdamond_check_vm_accesses(struct damon_ctx *ctx);
bool kdamond_vm_target_valid(struct damon_target *t);

/* Reference callback implementations for physical memory */
void kdamond_init_phys_regions(struct damon_ctx *ctx);
void kdamond_update_phys_regions(struct damon_ctx *ctx);
void kdamond_prepare_phys_access_checks(struct damon_ctx *ctx);
unsigned int kdamond_check_phys_accesses(struct damon_ctx *ctx);

int damon_set_targets(struct damon_ctx *ctx,
		unsigned long *ids, ssize_t nr_ids);
int damon_set_attrs(struct damon_ctx *ctx, unsigned long sample_int,
		unsigned long aggr_int, unsigned long regions_update_int,
		unsigned long min_nr_reg, unsigned long max_nr_reg);
int damon_set_schemes(struct damon_ctx *ctx,
			struct damos **schemes, ssize_t nr_schemes);
int damon_set_recording(struct damon_ctx *ctx,
				unsigned int rbuf_len, char *rfile_path);
int damon_start(struct damon_ctx *ctx);
int damon_stop(struct damon_ctx *ctx);

#endif
