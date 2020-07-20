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
 * struct damon_target - Represents a monitoring target.
 * @id:                 Unique identifier for this target.
 * @regions_list:       Head of the monitoring target regions of this target.
 * @list:               List head for siblings.
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
 * struct damon_ctx - Represents a context for each monitoring.
 * @targets_list:       Head of monitoring targets (&damon_target) list.
 */
struct damon_ctx {
	struct list_head targets_list;	/* 'damon_target' objects */
};

#endif
