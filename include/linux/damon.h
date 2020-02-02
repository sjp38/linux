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

struct damon_ctx {
	struct list_head tasks_list;	/* 'damon_task' objects */
};

#endif
