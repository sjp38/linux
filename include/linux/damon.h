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
 * struct damon_region - Represents a monitoring target region of
 * [@vm_start, @vm_end).
 *
 * @vm_start:		Start address of the region (inclusive).
 * @vm_end:		End address of the region (exclusive).
 * @sampling_addr:	Address of the sample for the next access check.
 * @nr_accesses:	Access frequency of this region.
 * @list:		List head for siblings.
 */
struct damon_region {
	unsigned long vm_start;
	unsigned long vm_end;
	unsigned long sampling_addr;
	unsigned int nr_accesses;
	struct list_head list;
};

/**
 * struct damon_task - Represents a monitoring target task.
 * @pid:		Process id of the task.
 * @regions_list:	Head of the monitoring target regions of this task.
 * @list:		List head for siblings.
 */
struct damon_task {
	int pid;
	struct list_head regions_list;
	struct list_head list;
};

/**
 * struct damon_ctx - Represents a context for each monitoring.  This is the
 * main interface that allows users to set the attributes and get the results
 * of the monitoring.
 *
 * For each monitoring request (damon_start()), a kernel thread for the
 * monitoring is created.  The pointer to the thread is stored in @kdamond.
 *
 * @sample_interval:		The time between access samplings.
 * @aggr_interval:		The time between monitor results aggregations.
 * @min_nr_regions:		The number of initial monitoring regions.
 *
 * For each @sample_interval, DAMON checks whether each region is accessed or
 * not.  It aggregates and keeps the access information (number of accesses to
 * each region) for @aggr_interval time.  All time intervals are in
 * micro-seconds.
 *
 * @kdamond:		Kernel thread who does the monitoring.
 * @kdamond_stop:	Notifies whether kdamond should stop.
 * @kdamond_lock:	Mutex for the synchronizations with @kdamond.
 *
 * The monitoring thread sets @kdamond to NULL when it terminates.  Therefore,
 * users can know whether the monitoring is ongoing or terminated by reading
 * @kdamond.  Also, users can ask @kdamond to be terminated by writing non-zero
 * to @kdamond_stop.  Reads and writes to @kdamond and @kdamond_stop from
 * outside of the monitoring thread must be protected by @kdamond_lock.
 *
 * Note that the monitoring thread protects only @kdamond and @kdamond_stop via
 * @kdamond_lock.  Accesses to other fields must be protected by themselves.
 *
 * @tasks_list:		Head of monitring target tasks (&damon_task) list.
 */
struct damon_ctx {
	unsigned long sample_interval;
	unsigned long aggr_interval;
	unsigned long min_nr_regions;

	struct timespec64 last_aggregation;

	struct task_struct *kdamond;
	bool kdamond_stop;
	struct mutex kdamond_lock;

	struct list_head tasks_list;	/* 'damon_task' objects */
};

int damon_set_pids(struct damon_ctx *ctx, int *pids, ssize_t nr_pids);
int damon_set_attrs(struct damon_ctx *ctx, unsigned long sample_int,
		unsigned long aggr_int, unsigned long min_nr_reg);
int damon_start(struct damon_ctx *ctx);
int damon_stop(struct damon_ctx *ctx);

#endif
