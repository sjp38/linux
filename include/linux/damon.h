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

/* Represents a monitoring target region of [vm_start, vm_end) */
struct damon_region {
	unsigned long vm_start;
	unsigned long vm_end;
	unsigned long sampling_addr;
	unsigned int nr_accesses;
	struct list_head list;
};

/* Represents a monitoring target task */
struct damon_task {
	int pid;
	struct list_head regions_list;
	struct list_head list;
};

/*
 * For each 'sample_interval', DAMON checks whether each region is accessed or
 * not.  It aggregates and keeps the access information (number of accesses to
 * each region) for each 'aggr_interval' time.
 *
 * All time intervals are in micro-seconds.
 */
struct damon_ctx {
	unsigned long sample_interval;
	unsigned long aggr_interval;
	unsigned long min_nr_regions;

	struct timespec64 last_aggregation;

	struct task_struct *kdamond;
	struct mutex kdamond_lock;

	struct list_head tasks_list;	/* 'damon_task' objects */
};

int damon_set_pids(struct damon_ctx *ctx, unsigned long *pids, ssize_t nr_pids);
int damon_set_attrs(struct damon_ctx *ctx, unsigned long sample_int,
		unsigned long aggr_int, unsigned long min_nr_reg);
int damon_start(struct damon_ctx *ctx);
int damon_stop(struct damon_ctx *ctx);

#endif
