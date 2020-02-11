/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DAMON api
 *
 * Copyright 2019 Amazon.com, Inc. or its affiliates.  All rights reserved.
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 */

#ifndef _DAMON_H_
#define _DAMON_H_

#include <linux/random.h>
#include <linux/spinlock_types.h>
#include <linux/time64.h>
#include <linux/types.h>

/* Represents a monitoring target region on the virtual address space */
struct damon_region {
	unsigned long vm_start;
	unsigned long vm_end;
	unsigned long sampling_addr;
	unsigned int nr_accesses;
	struct list_head list;

	unsigned int age;
	unsigned long last_vm_start;
	unsigned long last_vm_end;
	unsigned int last_nr_accesses;
};

/* Represents a monitoring target task */
struct damon_task {
	unsigned long pid;
	struct list_head regions_list;
	struct list_head list;
};

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
	spinlock_t kdamond_lock;

	struct rnd_state rndseed;

	struct list_head tasks_list;	/* 'damon_task' objects */

	/* callbacks */
	void (*sample_cb)(struct damon_ctx *context);
	void (*aggregate_cb)(struct damon_ctx *context);
};

int damon_set_pids(struct damon_ctx *ctx,
			unsigned long *pids, ssize_t nr_pids);
int damon_set_recording(struct damon_ctx *ctx,
			unsigned int rbuf_len, char *rfile_path);
int damon_set_attrs(struct damon_ctx *ctx, unsigned long s, unsigned long a,
			unsigned long r, unsigned long min, unsigned long max);
int damon_start(struct damon_ctx *ctx);
int damon_stop(struct damon_ctx *ctx);

#endif
