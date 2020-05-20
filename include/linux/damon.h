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
 * struct damon_addr_range - Represents an address region.
 * @start:	Start address of the region (Inclusive).
 * @end:	End address of the region (Exclusive).
 *
 * Note that this is a half-open interval of [start, end).
 */
struct damon_addr_range {
	unsigned long start;
	unsigned long end;
};

/**
 * struct damon_region - Represents a monitoring target region.
 * @ar:			Address range of the region.
 * @sampling_addr:	Address of the sample for access check.
 * @nr_accesses:	Access frequency of this region.
 * @age:		Age of this region.
 * @last_nr_accesses:	Internal value for age calculation.
 *
 * Age of the region is initially zero, increased for each aggregation
 * interval, and reset if the access frequency is significantly changed.  In
 * case of merge, age is calculated again with size-weighted way.
 */
struct damon_region {
	struct damon_addr_range ar;
	unsigned long sampling_addr;
	unsigned int nr_accesses;
	struct list_head list;

	unsigned int age;
	unsigned int last_nr_accesses;
};

/** struct damon_task - Represents a monitoring target task.
 * @pid:	Process id of the task.
 */
struct damon_task {
	int pid;
	struct list_head regions_list;
	struct list_head list;
};

/**
 * enum damos_action - Represents an action of a Data Access Monitoring-based
 * Operation Scheme.
 *
 * @DAMOS_WILLNEED:	Call madvise() for the region with MADV_WILLNEED.
 * @DAMOS_COLD:		Call madvise() for the region with MADV_COLD.
 * @DAMOS_PAGEOUT:	Call madvise() for the region with MADV_PAGEOUT.
 * @DAMOS_HUGEPAGE:	Call madvise() for the region with MADV_HUGEPAGE.
 * @DAMOS_NOHUGEPAGE:	Call madvise() for the region with MADV_NOHUGEPAGE.
 * @DAMOS_STAT:		Do nothing but count the stat.
 */
enum damos_action {
	DAMOS_WILLNEED,
	DAMOS_COLD,
	DAMOS_PAGEOUT,
	DAMOS_HUGEPAGE,
	DAMOS_NOHUGEPAGE,
	DAMOS_STAT,		/* Do nothing but only record the stat */
	DAMOS_ACTION_LEN,
};

/**
 * struct damos - Represents a Data Access Monitoring-based Operation Scheme.
 * @min_sz_region:	Minimum size of target regions.
 * @max_sz_region:	Maximum size of target regions.
 * @min_nr_accesses:	Minimum '->nr_accesses' of target regions.
 * @max_nr_accesses:	Maximum '->nr_accesses' of target regions.
 * @min_age_region:	Minimum age of target regions.
 * @max_age_region:	Maximum age of target regions.
 * @action:		'damo_action' to be applied to the target regions.
 * @stat_count:		Total number of regions that this scheme is applied.
 * @stat_sz:		Total size of regions that this scheme is applied.
 * @list:		Link for sibling schemes.
 */
struct damos {
	unsigned int min_sz_region;
	unsigned int max_sz_region;
	unsigned int min_nr_accesses;
	unsigned int max_nr_accesses;
	unsigned int min_age_region;
	nunsigned int max_age_region;
	enum damos_action action;
	unsigned long stat_count;
	unsigned long stat_sz;
	struct list_head list;
};

/**
 * struct damon_ctx - Represents a context for each monitoring.  This is the
 * main interface that allows users set the attributes and get the results of
 * the monitoring.
 *
 * @sample_interval:		Time between each access sampling.
 * @aggr_interval:		Time between monitoring results aggregation.
 * @regions_update_interval:	Time between monitoring target region update.
 * @min_nr_regions:		Number of initial monitoring target regions.
 * @max_nr_regions:		Maximum number of monitoring target regions.
 *
 * For each @sample_interval, DAMON checks whether each region is accessed or
 * not.  It aggregates and keeps the access information (number of accesses to
 * each region) for @aggr_interval time.  DAMON also checks whether the target
 * memory regions need update (e.g., by mmap() calls from the application, in
 * case of virtual memory monitoring) and applies the changes for each
 * @regions_update_interval.
 * All time intervals are in micro-seconds.
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
 * @tasks_list:		Head of 'struct damon_task' list.
 * @schemes_list:	Head of 'struct damos' list.
 *
 * @init_target_regions:	Constructs initial monitoring target regions.
 * @update_target_regions:	Updates monitoring target regions.
 * @prepare_access_checks:	Prepares next access check of target regions.
 * @check_accesses:		Checks the access of target regions.
 * @sample_cb:			Called for each sampling interval.
 * @aggregate_cb:		Called for each aggregation interval.
 *
 * By setting @init_target_regions, @update_target_regions,
 * @prepare_access_checks, and @check_accesses to appropriate functions, users
 * can monitor any address space with special handling.
 *
 * @sample_cb and @aggregate_cb are the recommended place for access to of the
 * monitorin results.  Because these callbacks are called by @kdamond, users
 * can access the monitoring results via @tasks_list without additional
 * protection of @kdamond_lock.
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

	struct list_head tasks_list;	/* 'damon_task' objects */
	struct list_head schemes_list;	/* 'damos' objects */

	/* callbacks */
	void (*init_target_regions)(struct damon_ctx *context);
	void (*update_target_regions)(struct damon_ctx *context);
	void (*prepare_access_checks)(struct damon_ctx *context);
	unsigned int (*check_accesses)(struct damon_ctx *context);
	void (*sample_cb)(struct damon_ctx *context);
	void (*aggregate_cb)(struct damon_ctx *context);
};

int damon_set_pids(struct damon_ctx *ctx, int *pids, ssize_t nr_pids);
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
