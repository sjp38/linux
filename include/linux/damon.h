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
 */
struct damon_region {
	struct damon_addr_range ar;
	unsigned long sampling_addr;
	unsigned int nr_accesses;
	struct list_head list;
};

/**
 * struct damon_task - Represents a monitoring target task.
 * @pid:		Process id of the task.
 * @regions_list:	Head of the monitoring target regions of this task.
 * @list:		List head for siblings.
 *
 * If the monitoring target address space is task independent (e.g., physical
 * memory address space monitoring), @pid should be '-1'.
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
 * @regions_update_interval:	The time between monitor regions updates.
 * @min_nr_regions:		The number of initial monitoring regions.
 * @max_nr_regions:		The maximum number of monitoring regions.
 *
 * For each @sample_interval, DAMON checks whether each region is accessed or
 * not.  It aggregates and keeps the access information (number of accesses to
 * each region) for @aggr_interval time.  DAMON also checks whether the target
 * memory regions need update (e.g., by ``mmap()`` calls from the application,
 * in case of virtual memory monitoring) and applies the changes for each
 * @regions_update_interval.  All time intervals are in micro-seconds.
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
 * @tasks_list:		Head of monitoring target tasks (&damon_task) list.
 *
 * @init_target_regions:	Constructs initial monitoring target regions.
 * @update_target_regions:	Updates monitoring target regions.
 * @prepare_access_checks:	Prepares next access check of target regions.
 * @check_accesses:		Checks the access of target regions.
 * @sample_cb:			Called for each sampling interval.
 * @aggregate_cb:		Called for each aggregation interval.
 *
 * DAMON can be extended for various address spaces by users.  For this, users
 * can register the target address space dependent low level functions for
 * their usecases via the callback pointers of the context.  The monitoring
 * thread calls @init_target_regions before starting the monitoring,
 * @update_target_regions for each @regions_update_interval, and
 * @prepare_access_checks and @check_accesses for each @sample_interval.
 *
 * @init_target_regions should construct proper monitoring target regions and
 * link those to the DAMON context struct.
 * @update_target_regions should update the monitoring target regions for
 * current status.
 * @prepare_access_checks should manipulate the monitoring regions to be
 * prepare for the next access check.
 * @check_accesses should check the accesses to each region that made after the
 * last preparation and update the `->nr_accesses` of each region.
 *
 * @sample_cb and @aggregate_cb are called from @kdamond for each of the
 * sampling intervals and aggregation intervals, respectively.  Therefore,
 * users can safely access to the monitoring results via @tasks_list without
 * additional protection of @kdamond_lock.  For the reason, users are
 * recommended to use these callback for the accesses to the results.
 */
struct damon_ctx {
	unsigned long sample_interval;
	unsigned long aggr_interval;
	unsigned long regions_update_interval;
	unsigned long min_nr_regions;
	unsigned long max_nr_regions;

	struct timespec64 last_aggregation;
	struct timespec64 last_regions_update;

	struct task_struct *kdamond;
	bool kdamond_stop;
	struct mutex kdamond_lock;

	struct list_head tasks_list;	/* 'damon_task' objects */

	/* callbacks */
	void (*init_target_regions)(struct damon_ctx *context);
	void (*update_target_regions)(struct damon_ctx *context);
	void (*prepare_access_checks)(struct damon_ctx *context);
	unsigned int (*check_accesses)(struct damon_ctx *context);
	void (*sample_cb)(struct damon_ctx *context);
	void (*aggregate_cb)(struct damon_ctx *context);
};

/* Reference callback implementations for virtual memory */
void kdamond_init_vm_regions(struct damon_ctx *ctx);
void kdamond_update_vm_regions(struct damon_ctx *ctx);
void kdamond_prepare_vm_access_checks(struct damon_ctx *ctx);
unsigned int kdamond_check_vm_accesses(struct damon_ctx *ctx);

int damon_set_pids(struct damon_ctx *ctx, int *pids, ssize_t nr_pids);
int damon_set_attrs(struct damon_ctx *ctx, unsigned long sample_int,
		unsigned long aggr_int, unsigned long regions_update_int,
		unsigned long min_nr_reg, unsigned long max_nr_reg);
int damon_start(struct damon_ctx *ctx);
int damon_stop(struct damon_ctx *ctx);

#endif
