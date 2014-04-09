/*
 * eval_gcma.c - evaluate guaranteed contiguous memory allocator performance
 *
 * gcma is a contiguous memory allocator which guarantees success and
 * maximum wait time for allocation request.
 * It secure large amount of memory and let it be allocated to the
 * contiguous memory request while it can be used as backend for
 * frontswap and cleancache concurrently.
 *
 * This module evaluate performance of gcma by trying contiguous memory
 * allocation via cma and gcma and compare the result while stressing memory.
 *
 * Copyright (C) 2014   Minchan Kim <minchan@kernel.org>
 *                      SeongJae Park <sj38.park@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/gcma.h>
#include <linux/mm.h>
#include <linux/dma-contiguous.h>

/**
 * returns 0 if failed to alloc
 */
static unsigned long measure_gcma(void)
{
	struct page *page;
	struct timespec start, end;
	getnstimeofday(&start);
	page = gcma_alloc_contig(0, 10);
	if (!page)
		return 0;

	getnstimeofday(&end);
	gcma_release_contig(0, page, 10);

	return end.tv_sec * 1000000000 + end.tv_nsec
		- (start.tv_sec * 1000000000 + start.tv_nsec);
}

/**
 * returns 0 if failed to alloc
 */
static unsigned long measure_cma(void)
{
	struct page *page;
	struct timespec start, end;
	getnstimeofday(&start);
	page = dma_alloc_from_contiguous(NULL, 10, 1);
	if (!page)
		return 0;

	getnstimeofday(&end);
	dma_release_from_contiguous(NULL, page, 10);

	return end.tv_sec * 1000000000 + end.tv_nsec
		- (start.tv_sec * 1000000000 + start.tv_nsec);
}

static unsigned long min_of(unsigned long prev_min, unsigned long new_val)
{
	if (prev_min == 0 || prev_min > new_val)
		return new_val;
	return prev_min;
}

static unsigned long max_of(unsigned long prev_max, unsigned long new_val)
{
	if (prev_max == 0 || prev_max < new_val)
		return new_val;
	return prev_max;
}

static int measure_time_alloc(void)
{
	unsigned long nr_gcma_fail = 0, nr_cma_fail = 0;
	unsigned long gcma_time, gcma_min = 0, gcma_max = 0, gcma_avg;
	unsigned long cma_time, cma_min = 0, cma_max = 0, cma_avg = 0;
	unsigned long gcma_sum = 0, cma_sum = 0;
	int measure_count = 500, i;

	for (i = 0; i < measure_count; i++) {
		cma_time = measure_cma();
		gcma_time = measure_gcma();

		if (!cma_time)
			nr_cma_fail++;
		if (!gcma_time)
			nr_gcma_fail++;

		gcma_min = min_of(gcma_min, gcma_time);
		gcma_max = max_of(gcma_max, gcma_time);
		gcma_sum += gcma_time;

		cma_min = min_of(cma_min, cma_time);
		cma_max = max_of(cma_max, cma_time);
		cma_sum += cma_time;
	}
	gcma_avg = gcma_sum / measure_count;
	cma_avg = cma_sum / measure_count;


	pr_info("gcma\tfail: %ld\tmin: %ld\tmax: %ld\tavg: %ld\n",
			nr_gcma_fail, gcma_min, gcma_max, gcma_avg);
	pr_info("cma\tfail: %ld\tmin: %ld\tmax: %ld\tavg: %ld\n",
			nr_cma_fail, cma_min, cma_max, cma_avg);

	return 0;
}

/*********************************
 * module init and exit
 **********************************/
static int __init init_eval_gcma(void)
{
	pr_info("eval gcma\n");

	measure_time_alloc();

	return 0;
}

static void exit_eval_gcma(void)
{
	pr_info("unload gcma evaluator\n");
}

module_init(init_eval_gcma);
module_exit(exit_eval_gcma);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Minchan Kim <minchan@kernel.org>");
MODULE_AUTHOR("SeongJae Park <sj38.park@gmail.com>");
MODULE_DESCRIPTION("Evaluation for Guaranteed contiguous memory allocator");
